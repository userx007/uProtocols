# DDS Message Decoder

It is entirely possible to implement a DDS message decoder that displays the contents of messages, including headers, sub-messages, fields, and their values. Here's a comprehensive overview:

## How DDS Messages Are Structured

DDS messages (especially in **DDS-RTPS** or **DDS-RMW** transports) follow a well-defined binary format:

```
[Transport Header]
[RTPS Header]
[Submessage Header(s)]
[Data / Payload]
```

Each submessage type has a known structure defined by the [DDS-RTPS specification](https://www.omg.org/spec/DDSI-RTPS/).

## Implementation Approaches

### 1. **Low-Level Custom Decoder**
Parse the binary RTPS/DDS wire format manually:

```python
class DDSMessageDecoder:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read_uint8(self):
        val = self.data[self.offset]
        self.offset += 1
        return val

    def read_uint16(self):
        val = int.from_bytes(self.data[self.offset:self.offset+2], 'little')
        self.offset += 2
        return val

    def read_submessage_header(self):
        msg_type = self.read_uint8()
        flags = self.read_uint8()
        length = self.read_uint16()
        return {
            'type': msg_type,
            'flags': flags,
            'length': length,
            'offset_before': self.offset
        }

    def decode(self):
        messages = []
        while self.offset < len(self.data):
            sub_header = self.read_submessage_header()
            sub_type = sub_header['type']
            msg_end = sub_header['offset_before'] + sub_header['length']

            submessage = {
                'type': sub_type,
                'type_name': self._get_submessage_name(sub_type),
                'flags': sub_header['flags'],
                'content': self._parse_submessage(sub_type, sub_header)
            }
            messages.append(submessage)
            self.offset = min(msg_end, len(self.data))
        return messages

    def _get_submessage_name(self, msg_type):
        names = {
            0x01: 'ACKNACK',
            0x02: 'NACKFRAG',
            0x03: 'HEARTBEAT',
            0x04: 'Gap',
            0x05: 'DATA',
            0x06: 'DATA_FRAG',
            0x07: 'TIME_STAMP',
            0x08: 'SOURCE_DESCRIPTOR',
            0x09: 'END_OF_BUNDLE',
            0x0A: 'BUNDLE',
            0x0B: 'PARAMETER',
            0x0C: 'DATA_REF',
            0x0D: 'EXTENDED_NACK',
            0x0E: 'SILENT_HEARTBEAT',
            0x0F: 'SILENT_ACKNACK',
            0x10: 'SILENT_GAP',
            0x11: 'SILENT_END_OF_BUNDLE',
            0x12: 'SILENT_BUNDLE',
            0x13: 'SILENT_PARAM',
            0x14: 'SILENT_DATA',
            0x15: 'SILENT_DATA_FRAG',
            0x16: 'SILENT_DATA_REF',
            0x17: 'SILENT_TIME_STAMP',
            0x18: 'SILENT_SOURCE_DESCRIPTOR',
        }
        return names.get(msg_type, f'UNKNOWN(0x{msg_type:02X})')

    def _parse_submessage(self, msg_type, header):
        content = {}
        if msg_type == 0x03:  # HEARTBEAT
            content['writer_guid'] = self.data[self.offset:self.offset+12]
            self.offset += 12
            content['sequence_number'] = self.read_uint32()
            content['heartbeat_count'] = self.read_uint32()
        elif msg_type == 0x05:  # DATA
            content['writer_guid'] = self.data[self.offset:self.offset+12]
            self.offset += 12
            content['topic_name'] = self._read_string()
            content['type_name'] = self._read_string()
            content['payload'] = self._read_payload()
        # ... handle other submessage types
        return content
```

### 2. **Using DDS Middleware APIs**

Most DDS vendors provide tools:

| Vendor | Tool/Feature |
|--------|-------------|
| **Eclipse Cyclone DDS** | `dds_info`, `dds_monitor`, `ddsdump` |
| **RTI Connext** | `rtiddsprofiler`, `dds2pcap`, XML Schema introspection |
| **OpenDDS** | `DDS2PCAP`, trace files |
| **Fast DDS** | `fastrtps`, `dds_monitor` |

### 3. **PCAP/Network Capture Integration**

Decode RTPS over UDP from packet captures:

```python
import dpkt
import socket

def parse_rtps_from_pcap(pcap_file):
    with open(pcap_file, 'rb') as f:
        pcap = dpkt.pcap.Reader(f)
        for ts, buf in pcap:
            eth = dpkt.ethernet.Ethernet(buf)
            if isinstance(eth.data, dpkt.ip.IP):
                ip = eth.data
                if isinstance(ip.data, dpkt.udp.UDP):
                    udp = ip.data
                    if udp.dport == 7400:  # Default RTPS port
                        decoder = DDSMessageDecoder(udp.data)
                        messages = decoder.decode()
                        for msg in messages:
                            print(json.dumps(msg, indent=2, default=str))
```

### 4. **Schema-Based Payload Decoding**

For meaningful content, you need the **Type Code** and **Serialized Data**:

```python
def decode_serialized_payload(payload_bytes, type_code):
    """
    Use the DDS Type Code (CDR-encoded) to interpret the binary payload.
    This requires:
    - Parsing the Type Code to understand nested structures, enums, arrays, etc.
    - Serializing/deserializing based on the Type Code layout.
    """
    type_decoder = DDSTypeDecoder(type_code)
    return type_decoder.deserialize(payload_bytes)
```

## Key Submessage Types to Handle

| Type | Code | Description |
|------|------|-------------|
| RTPS Header | — | Version, flags, GUID |
| ACKNACK | 0x01 | Acknowledgment of data |
| HEARTBEAT | 0x03 | Heartbeat from writer |
| DATA | 0x05 | Actual user message |
| DATA_FRAG | 0x06 | Fragmented data |
| TIME_STAMP | 0x07 | Timestamp submessage |
| PARAMETER | 0x0B | Configuration parameters |

## Tools & Libraries

- **[dds2pcap](https://github.com/eclipse-cyclonedds/cyclonedds/blob/master/tools/dds2pcap/)** – Converts DDS traffic to PCAP
- **[DDS Explorer](https://github.com/rti-community/dds-explorer)** – GUI for DDS
- **[DDS Monitor](https://community.rti.com/docs/)** – RTI's monitoring tool
- **[OpenDDS Trace](https://opendds.org/)** – Logging/tracing
- **[pydds](https://github.com/)** – Python DDS libraries with introspection
- **[DDS-XSD-Parser](https://www.omg.org/spec/DDSXSD/)** – For schema-based decoding

## Challenges

1. **Type Code Resolution** – You need the Type Code (or schema) to decode payload fields meaningfully.
2. **Fragment Reassembly** – Large messages may span multiple `DATA_FRAG` submessages.
3. **Multiple Transport Layers** – DDS can run over UDP, TCP, SHM, etc.
4. **Dynamic Types** – DDS-SPS supports dynamic types, requiring runtime type resolution.

## Summary

| Aspect | Feasibility |
|--------|-------------|
| Parse submessage headers | ✅ Easy |
| Display GUIDs, sequence numbers | ✅ Easy |
| Decode raw payload bytes | ✅ Easy |
| Decode structured fields | ⚠️ Requires Type Code/Schema |
| Full bidirectional decode | ✅ With proper schema |

