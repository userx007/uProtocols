use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum Opcode {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
}

impl Opcode {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x0 => Some(Opcode::Continuation),
            0x1 => Some(Opcode::Text),
            0x2 => Some(Opcode::Binary),
            0x8 => Some(Opcode::Close),
            0x9 => Some(Opcode::Ping),
            0xA => Some(Opcode::Pong),
            _ => None,
        }
    }

    fn is_control(&self) -> bool {
        (*self as u8) >= 0x8
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
enum CloseCode {
    Normal = 1000,
    GoingAway = 1001,
    ProtocolError = 1002,
    InvalidData = 1003,
    InvalidPayload = 1007,
    PolicyViolation = 1008,
    MessageTooBig = 1009,
}

#[derive(Debug)]
struct ProtocolViolation {
    code: CloseCode,
    message: String,
}

impl ProtocolViolation {
    fn new(code: CloseCode, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}

impl fmt::Display for ProtocolViolation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} (code: {})", self.message, self.code as u16)
    }
}

impl std::error::Error for ProtocolViolation {}

type Result<T> = std::result::Result<T, ProtocolViolation>;

#[derive(Debug, Clone)]
struct FrameHeader {
    fin: bool,
    rsv1: bool,
    rsv2: bool,
    rsv3: bool,
    opcode: Opcode,
    masked: bool,
    payload_length: u64,
    masking_key: Option<[u8; 4]>,
}

impl FrameHeader {
    fn new(opcode: Opcode) -> Self {
        Self {
            fin: true,
            rsv1: false,
            rsv2: false,
            rsv3: false,
            opcode,
            masked: false,
            payload_length: 0,
            masking_key: None,
        }
    }
}

struct ConnectionState {
    in_fragmented_message: bool,
    fragmented_opcode: Option<Opcode>,
    is_server: bool,
    extensions_negotiated: bool,
}

impl ConnectionState {
    fn new(is_server: bool) -> Self {
        Self {
            in_fragmented_message: false,
            fragmented_opcode: None,
            is_server,
            extensions_negotiated: false,
        }
    }

    fn reset_fragmentation(&mut self) {
        self.in_fragmented_message = false;
        self.fragmented_opcode = None;
    }
}

struct WebSocketValidator {
    state: ConnectionState,
}

impl WebSocketValidator {
    fn new(is_server: bool) -> Self {
        Self {
            state: ConnectionState::new(is_server),
        }
    }

    fn validate_frame_header(&mut self, header: &FrameHeader) -> Result<()> {
        // Validate reserved bits
        if !self.state.extensions_negotiated
            && (header.rsv1 || header.rsv2 || header.rsv3)
        {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Reserved bits set without negotiated extension",
            ));
        }

        // Control frames must not be fragmented
        if header.opcode.is_control() && !header.fin {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Control frames cannot be fragmented",
            ));
        }

        // Control frames payload size limit
        if header.opcode.is_control() && header.payload_length > 125 {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                format!(
                    "Control frame payload exceeds 125 bytes: {}",
                    header.payload_length
                ),
            ));
        }

        // Validate masking
        self.validate_masking(header)?;

        // Validate fragmentation
        self.validate_fragmentation(header)?;

        Ok(())
    }

    fn validate_masking(&self, header: &FrameHeader) -> Result<()> {
        if self.state.is_server && !header.masked {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Client frames must be masked",
            ));
        }

        if !self.state.is_server && header.masked {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Server frames must not be masked",
            ));
        }

        Ok(())
    }

    fn validate_fragmentation(&mut self, header: &FrameHeader) -> Result<()> {
        match header.opcode {
            Opcode::Continuation => {
                if !self.state.in_fragmented_message {
                    return Err(ProtocolViolation::new(
                        CloseCode::ProtocolError,
                        "Continuation frame without initial frame",
                    ));
                }

                if header.fin {
                    self.state.reset_fragmentation();
                }
            }
            Opcode::Text | Opcode::Binary => {
                if self.state.in_fragmented_message {
                    return Err(ProtocolViolation::new(
                        CloseCode::ProtocolError,
                        "New message started during active fragmentation",
                    ));
                }

                if !header.fin {
                    self.state.in_fragmented_message = true;
                    self.state.fragmented_opcode = Some(header.opcode);
                }
            }
            _ => {} // Control frames
        }

        Ok(())
    }

    fn validate_text_payload(&self, payload: &[u8]) -> Result<()> {
        if !is_valid_utf8(payload) {
            return Err(ProtocolViolation::new(
                CloseCode::InvalidPayload,
                "Text frame contains invalid UTF-8",
            ));
        }
        Ok(())
    }

    fn validate_close_payload(&self, payload: &[u8]) -> Result<()> {
        if payload.is_empty() {
            return Ok(());
        }

        if payload.len() == 1 {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                "Close payload must be 0 or at least 2 bytes",
            ));
        }

        // Extract close code (big-endian)
        let close_code = u16::from_be_bytes([payload[0], payload[1]]);

        // Validate close code ranges
        if close_code < 1000
            || (close_code >= 1004 && close_code <= 1006)
            || (close_code >= 1012 && close_code <= 2999)
        {
            return Err(ProtocolViolation::new(
                CloseCode::ProtocolError,
                format!("Invalid close code: {}", close_code),
            ));
        }

        // Validate UTF-8 reason
        if payload.len() > 2 && !is_valid_utf8(&payload[2..]) {
            return Err(ProtocolViolation::new(
                CloseCode::InvalidPayload,
                "Close reason contains invalid UTF-8",
            ));
        }

        Ok(())
    }
}

fn is_valid_utf8(data: &[u8]) -> bool {
    std::str::from_utf8(data).is_ok()
}

struct WebSocketConnection {
    validator: WebSocketValidator,
}

impl WebSocketConnection {
    fn new(is_server: bool) -> Self {
        Self {
            validator: WebSocketValidator::new(is_server),
        }
    }

    fn process_frame(&mut self, header: &FrameHeader, payload: &[u8]) {
        match self.validate_and_process(header, payload) {
            Ok(_) => println!("✓ Frame processed successfully"),
            Err(violation) => self.handle_protocol_violation(violation),
        }
    }

    fn validate_and_process(
        &mut self,
        header: &FrameHeader,
        payload: &[u8],
    ) -> Result<()> {
        // Validate frame header
        self.validator.validate_frame_header(header)?;

        // Additional payload validation
        match header.opcode {
            Opcode::Text => self.validator.validate_text_payload(payload)?,
            Opcode::Close => self.validator.validate_close_payload(payload)?,
            _ => {}
        }

        Ok(())
    }

    fn handle_protocol_violation(&mut self, violation: ProtocolViolation) {
        eprintln!("✗ Protocol violation: {}", violation);
        self.send_close_frame(violation.code, &violation.message);
        self.close_connection();
    }

    fn send_close_frame(&self, code: CloseCode, reason: &str) {
        println!("  → Sending close frame: {} ({})", code as u16, reason);
        // Implementation would construct and send actual close frame
    }

    fn close_connection(&mut self) {
        println!("  → Connection closed");
        self.validator.state.reset_fragmentation();
        // Implementation would close socket
    }
}

fn main() {
    let mut connection = WebSocketConnection::new(true); // Server-side

    println!("=== Test 1: Valid text frame ===");
    let mut valid_frame = FrameHeader::new(Opcode::Text);
    valid_frame.masked = true;
    valid_frame.payload_length = 11;
    let payload = b"Hello World";
    connection.process_frame(&valid_frame, payload);

    println!("\n=== Test 2: Control frame fragmented (invalid) ===");
    let mut invalid_frame = FrameHeader::new(Opcode::Ping);
    invalid_frame.fin = false;
    invalid_frame.masked = true;
    connection.process_frame(&invalid_frame, &[]);

    println!("\n=== Test 3: Unmasked client frame (invalid) ===");
    let mut unmasked_frame = FrameHeader::new(Opcode::Text);
    unmasked_frame.masked = false;
    connection.process_frame(&unmasked_frame, b"test");

    println!("\n=== Test 4: Invalid UTF-8 in text frame ===");
    let mut text_frame = FrameHeader::new(Opcode::Text);
    text_frame.masked = true;
    let invalid_utf8 = &[0xFF, 0xFE];
    connection.process_frame(&text_frame, invalid_utf8);

    println!("\n=== Test 5: Valid close frame ===");
    let mut close_frame = FrameHeader::new(Opcode::Close);
    close_frame.masked = true;
    close_frame.payload_length = 5;
    let close_payload = &[0x03, 0xE8, b'B', b'y', b'e']; // 1000 + "Bye"
    connection.process_frame(&close_frame, close_payload);

    println!("\n=== Test 6: Fragmentation sequence ===");
    let mut start_frame = FrameHeader::new(Opcode::Text);
    start_frame.fin = false;
    start_frame.masked = true;
    connection.process_frame(&start_frame, b"Hello ");

    let mut cont_frame = FrameHeader::new(Opcode::Continuation);
    cont_frame.masked = true;
    connection.process_frame(&cont_frame, b"World");
}