use std::time::{Duration, Instant};
use std::collections::BinaryHeap;
use std::cmp::Ordering;

/// CAN message structure
#[derive(Debug, Clone)]
pub struct CanMessage {
    pub id: u32,
    pub data: [u8; 8],
    pub dlc: u8,
    pub timestamp: Instant,
}

impl CanMessage {
    pub fn new(id: u32) -> Self {
        Self {
            id,
            data: [0; 8],
            dlc: 0,
            timestamp: Instant::now(),
        }
    }
}

/// Priority wrapper for message queue (lower ID = higher priority)
#[derive(Debug, Clone)]
struct PriorityMessage(CanMessage);

impl PartialEq for PriorityMessage {
    fn eq(&self, other: &Self) -> bool {
        self.0.id == other.0.id
    }
}

impl Eq for PriorityMessage {}

impl PartialOrd for PriorityMessage {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for PriorityMessage {
    fn cmp(&self, other: &Self) -> Ordering {
        // Reverse ordering: lower ID = higher priority
        other.0.id.cmp(&self.0.id)
    }
}

/// Message schedule entry
pub struct ScheduleEntry {
    message: CanMessage,
    period: Option<Duration>,
    deadline: Duration,
    trigger: Option<Box<dyn Fn() -> bool + Send>>,
    data_provider: Option<Box<dyn Fn(&mut [u8; 8]) + Send>>,
}

impl ScheduleEntry {
    /// Create a time-triggered message
    pub fn time_triggered(
        id: u32,
        period: Duration,
        deadline: Duration,
        data_provider: impl Fn(&mut [u8; 8]) + Send + 'static,
    ) -> Self {
        Self {
            message: CanMessage::new(id),
            period: Some(period),
            deadline,
            trigger: None,
            data_provider: Some(Box::new(data_provider)),
        }
    }

    /// Create an event-triggered message
    pub fn event_triggered(
        id: u32,
        deadline: Duration,
        trigger: impl Fn() -> bool + Send + 'static,
        data_provider: Option<impl Fn(&mut [u8; 8]) + Send + 'static>,
    ) -> Self {
        Self {
            message: CanMessage::new(id),
            period: None,
            deadline,
            trigger: Some(Box::new(trigger)),
            data_provider: data_provider.map(|f| Box::new(f) as Box<_>),
        }
    }

    /// Create a hybrid message (both time and event triggered)
    pub fn hybrid(
        id: u32,
        period: Duration,
        deadline: Duration,
        trigger: impl Fn() -> bool + Send + 'static,
        data_provider: impl Fn(&mut [u8; 8]) + Send + 'static,
    ) -> Self {
        Self {
            message: CanMessage::new(id),
            period: Some(period),
            deadline,
            trigger: Some(Box::new(trigger)),
            data_provider: Some(Box::new(data_provider)),
        }
    }
}

/// CAN message scheduler
pub struct CanScheduler {
    entries: Vec<ScheduleEntry>,
    tx_queue: BinaryHeap<PriorityMessage>,
}

impl CanScheduler {
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
            tx_queue: BinaryHeap::new(),
        }
    }

    pub fn add_entry(&mut self, entry: ScheduleEntry) {
        self.entries.push(entry);
    }

    /// Time-triggered scheduling
    pub fn process_time_triggered(&mut self) {
        let now = Instant::now();

        for entry in &mut self.entries {
            if let Some(period) = entry.period {
                let elapsed = now.duration_since(entry.message.timestamp);

                if elapsed >= period {
                    self.prepare_message(entry, now);
                }
            }
        }
    }

    /// Event-triggered scheduling
    pub fn process_event_triggered(&mut self) {
        let now = Instant::now();

        for entry in &mut self.entries {
            if let Some(ref trigger) = entry.trigger {
                if trigger() {
                    let elapsed = now.duration_since(entry.message.timestamp);

                    // Check deadline constraint
                    if elapsed < entry.deadline {
                        self.prepare_message(entry, now);
                    }
                }
            }
        }
    }

    /// Hybrid scheduling - combines both approaches
    pub fn process_hybrid(&mut self) {
        let now = Instant::now();

        for entry in &mut self.entries {
            let mut should_send = false;

            // Check time-triggered condition
            if let Some(period) = entry.period {
                let elapsed = now.duration_since(entry.message.timestamp);
                if elapsed >= period {
                    should_send = true;
                }
            }

            // Check event-triggered condition
            if let Some(ref trigger) = entry.trigger {
                if trigger() {
                    should_send = true;
                }
            }

            if should_send {
                self.prepare_message(entry, now);
            }
        }
    }

    /// Prepare message for transmission
    fn prepare_message(&mut self, entry: &mut ScheduleEntry, now: Instant) {
        // Update data if provider exists
        if let Some(ref provider) = entry.data_provider {
            provider(&mut entry.message.data);
        }

        // Update timestamp
        entry.message.timestamp = now;

        // Add to priority queue
        self.tx_queue.push(PriorityMessage(entry.message.clone()));
    }

    /// Transmit all queued messages in priority order
    pub fn transmit_queue<F>(&mut self, mut transmit_fn: F)
    where
        F: FnMut(&CanMessage) -> Result<(), &'static str>,
    {
        while let Some(PriorityMessage(msg)) = self.tx_queue.pop() {
            match transmit_fn(&msg) {
                Ok(_) => {
                    println!("Transmitted CAN ID 0x{:03X}", msg.id);
                }
                Err(e) => {
                    eprintln!("Transmission failed: {}", e);
                    // Could re-queue or handle error
                }
            }
        }
    }

    /// Get number of queued messages
    pub fn queue_depth(&self) -> usize {
        self.tx_queue.len()
    }
}

// Example usage and data providers
fn engine_rpm_provider(data: &mut [u8; 8]) {
    let rpm: u16 = 3500; // Simulated RPM
    data[0] = (rpm >> 8) as u8;
    data[1] = (rpm & 0xFF) as u8;
}

fn temperature_provider(data: &mut [u8; 8]) {
    data[0] = 85; // 85°C
}

fn vehicle_speed_provider(data: &mut [u8; 8]) {
    let speed: u16 = 120; // 120 km/h
    data[0] = (speed >> 8) as u8;
    data[1] = (speed & 0xFF) as u8;
}

// Mock CAN transmit function
fn can_transmit(msg: &CanMessage) -> Result<(), &'static str> {
    // Platform-specific implementation would go here
    println!(
        "CAN TX: ID=0x{:03X}, DLC={}, Data={:02X?}",
        msg.id, msg.dlc, &msg.data[..msg.dlc as usize]
    );
    Ok(())
}

fn main() {
    let mut scheduler = CanScheduler::new();

    // High-priority engine RPM - time-triggered every 10ms
    scheduler.add_entry(ScheduleEntry::time_triggered(
        0x100,
        Duration::from_millis(10),
        Duration::from_millis(15),
        engine_rpm_provider,
    ));

    // Medium-priority temperature - time-triggered every 100ms
    scheduler.add_entry(ScheduleEntry::time_triggered(
        0x200,
        Duration::from_millis(100),
        Duration::from_millis(150),
        temperature_provider,
    ));

    // Low-priority speed - time-triggered every 50ms
    scheduler.add_entry(ScheduleEntry::time_triggered(
        0x300,
        Duration::from_millis(50),
        Duration::from_millis(75),
        vehicle_speed_provider,
    ));

    // Event-triggered button press
    let button_pressed = std::sync::Arc::new(std::sync::Mutex::new(false));
    let button_clone = button_pressed.clone();
    
    scheduler.add_entry(ScheduleEntry::event_triggered(
        0x400,
        Duration::from_millis(50),
        move || {
            let mut pressed = button_clone.lock().unwrap();
            let trigger = *pressed;
            *pressed = false; // Clear flag
            trigger
        },
        Some(|data: &mut [u8; 8]| {
            data[0] = 0x01; // Button press code
        }),
    ));

    // Simulation loop
    println!("Starting CAN message scheduler...\n");
    
    let start = Instant::now();
    let mut last_time = start;

    for _ in 0..100 {
        let now = Instant::now();
        
        // Simulate button press every 200ms
        if now.duration_since(last_time) >= Duration::from_millis(200) {
            *button_pressed.lock().unwrap() = true;
            last_time = now;
        }

        // Run hybrid scheduler
        scheduler.process_hybrid();

        // Transmit queued messages
        scheduler.transmit_queue(can_transmit);

        // Sleep for 1ms to simulate real-time operation
        std::thread::sleep(Duration::from_millis(1));
    }

    println!("\nScheduler demonstration complete.");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_priority_ordering() {
        let mut queue = BinaryHeap::new();
        
        queue.push(PriorityMessage(CanMessage::new(0x300)));
        queue.push(PriorityMessage(CanMessage::new(0x100)));
        queue.push(PriorityMessage(CanMessage::new(0x200)));

        assert_eq!(queue.pop().unwrap().0.id, 0x100);
        assert_eq!(queue.pop().unwrap().0.id, 0x200);
        assert_eq!(queue.pop().unwrap().0.id, 0x300);
    }

    #[test]
    fn test_time_triggered_scheduling() {
        let mut scheduler = CanScheduler::new();
        
        scheduler.add_entry(ScheduleEntry::time_triggered(
            0x100,
            Duration::from_millis(10),
            Duration::from_millis(15),
            |_| {},
        ));

        std::thread::sleep(Duration::from_millis(11));
        scheduler.process_time_triggered();
        
        assert_eq!(scheduler.queue_depth(), 1);
    }
}