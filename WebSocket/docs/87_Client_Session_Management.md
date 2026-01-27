# Client Session Management in WebSockets

## Detailed Description

Client Session Management is a critical aspect of WebSocket server design that involves tracking, maintaining, and managing the state and metadata associated with each connected client throughout their lifecycle. Unlike stateless HTTP connections, WebSocket connections are persistent, requiring servers to maintain detailed information about each client for the duration of their session.

### Key Components

**Session Identification**: Each client connection is assigned a unique identifier (session ID, connection ID, or token) that distinguishes it from other concurrent connections.

**Metadata Storage**: Associated with each session are various pieces of information:
- Connection timestamps (connect/disconnect times)
- Authentication credentials and authorization levels
- User profile data (username, user ID, roles)
- Client information (IP address, user agent, protocol version)
- Application-specific data (preferences, current state, subscriptions)
- Connection quality metrics (latency, message counts, error rates)

**Lifecycle Management**: Sessions transition through various states:
- **Establishment**: Initial handshake, authentication, and metadata initialization
- **Active**: Normal message exchange and state updates
- **Idle**: Connection maintained but no recent activity
- **Termination**: Graceful or abrupt closure with cleanup

**Concurrency Handling**: Managing multiple simultaneous sessions requires thread-safe data structures and efficient lookup mechanisms to route messages to the correct clients.

**Resource Management**: Tracking resource usage per session (bandwidth, message quotas, memory) to prevent abuse and ensure fair resource allocation.

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// Session metadata structure
typedef struct {
    char session_id[64];
    char username[50];
    char ip_address[46];  // IPv6 compatible
    time_t connect_time;
    time_t last_activity;
    int socket_fd;
    int authenticated;
    char role[20];
    long messages_sent;
    long messages_received;
    void* user_data;  // Application-specific data
} ClientSession;

// Session manager structure
typedef struct {
    ClientSession** sessions;
    int session_count;
    int capacity;
    pthread_mutex_t lock;
} SessionManager;

// Initialize session manager
SessionManager* session_manager_create(int initial_capacity) {
    SessionManager* mgr = (SessionManager*)malloc(sizeof(SessionManager));
    mgr->sessions = (ClientSession**)calloc(initial_capacity, sizeof(ClientSession*));
    mgr->session_count = 0;
    mgr->capacity = initial_capacity;
    pthread_mutex_init(&mgr->lock, NULL);
    return mgr;
}

// Generate unique session ID
void generate_session_id(char* buffer, size_t size) {
    snprintf(buffer, size, "sess_%ld_%d", time(NULL), rand());
}

// Add new session
ClientSession* session_add(SessionManager* mgr, int socket_fd, const char* ip) {
    pthread_mutex_lock(&mgr->lock);
    
    // Expand capacity if needed
    if (mgr->session_count >= mgr->capacity) {
        mgr->capacity *= 2;
        mgr->sessions = (ClientSession**)realloc(mgr->sessions, 
                                                  mgr->capacity * sizeof(ClientSession*));
    }
    
    ClientSession* session = (ClientSession*)malloc(sizeof(ClientSession));
    generate_session_id(session->session_id, sizeof(session->session_id));
    strncpy(session->ip_address, ip, sizeof(session->ip_address) - 1);
    session->socket_fd = socket_fd;
    session->connect_time = time(NULL);
    session->last_activity = time(NULL);
    session->authenticated = 0;
    session->messages_sent = 0;
    session->messages_received = 0;
    session->user_data = NULL;
    strcpy(session->role, "guest");
    strcpy(session->username, "");
    
    mgr->sessions[mgr->session_count++] = session;
    
    pthread_mutex_unlock(&mgr->lock);
    
    printf("Session created: %s (socket: %d, IP: %s)\n", 
           session->session_id, socket_fd, ip);
    
    return session;
}

// Find session by socket file descriptor
ClientSession* session_find_by_socket(SessionManager* mgr, int socket_fd) {
    pthread_mutex_lock(&mgr->lock);
    
    for (int i = 0; i < mgr->session_count; i++) {
        if (mgr->sessions[i]->socket_fd == socket_fd) {
            pthread_mutex_unlock(&mgr->lock);
            return mgr->sessions[i];
        }
    }
    
    pthread_mutex_unlock(&mgr->lock);
    return NULL;
}

// Find session by session ID
ClientSession* session_find_by_id(SessionManager* mgr, const char* session_id) {
    pthread_mutex_lock(&mgr->lock);
    
    for (int i = 0; i < mgr->session_count; i++) {
        if (strcmp(mgr->sessions[i]->session_id, session_id) == 0) {
            pthread_mutex_unlock(&mgr->lock);
            return mgr->sessions[i];
        }
    }
    
    pthread_mutex_unlock(&mgr->lock);
    return NULL;
}

// Update session activity
void session_update_activity(ClientSession* session) {
    session->last_activity = time(NULL);
}

// Authenticate session
void session_authenticate(ClientSession* session, const char* username, const char* role) {
    strncpy(session->username, username, sizeof(session->username) - 1);
    strncpy(session->role, role, sizeof(session->role) - 1);
    session->authenticated = 1;
    printf("Session %s authenticated as %s (role: %s)\n", 
           session->session_id, username, role);
}

// Remove session
void session_remove(SessionManager* mgr, const char* session_id) {
    pthread_mutex_lock(&mgr->lock);
    
    for (int i = 0; i < mgr->session_count; i++) {
        if (strcmp(mgr->sessions[i]->session_id, session_id) == 0) {
            printf("Removing session: %s (user: %s, duration: %ld seconds)\n",
                   mgr->sessions[i]->session_id,
                   mgr->sessions[i]->username,
                   time(NULL) - mgr->sessions[i]->connect_time);
            
            if (mgr->sessions[i]->user_data) {
                free(mgr->sessions[i]->user_data);
            }
            free(mgr->sessions[i]);
            
            // Shift remaining sessions
            for (int j = i; j < mgr->session_count - 1; j++) {
                mgr->sessions[j] = mgr->sessions[j + 1];
            }
            mgr->session_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&mgr->lock);
}

// Get all sessions for a specific user
int session_get_by_username(SessionManager* mgr, const char* username, 
                             ClientSession** results, int max_results) {
    pthread_mutex_lock(&mgr->lock);
    
    int count = 0;
    for (int i = 0; i < mgr->session_count && count < max_results; i++) {
        if (strcmp(mgr->sessions[i]->username, username) == 0) {
            results[count++] = mgr->sessions[i];
        }
    }
    
    pthread_mutex_unlock(&mgr->lock);
    return count;
}

// Print session statistics
void session_print_stats(SessionManager* mgr) {
    pthread_mutex_lock(&mgr->lock);
    
    printf("\n=== Session Statistics ===\n");
    printf("Total active sessions: %d\n", mgr->session_count);
    
    int authenticated = 0;
    for (int i = 0; i < mgr->session_count; i++) {
        if (mgr->sessions[i]->authenticated) authenticated++;
    }
    
    printf("Authenticated sessions: %d\n", authenticated);
    printf("Guest sessions: %d\n", mgr->session_count - authenticated);
    
    pthread_mutex_unlock(&mgr->lock);
}

// Cleanup expired sessions (idle timeout)
int session_cleanup_idle(SessionManager* mgr, int timeout_seconds) {
    pthread_mutex_lock(&mgr->lock);
    
    time_t now = time(NULL);
    int removed = 0;
    
    for (int i = mgr->session_count - 1; i >= 0; i--) {
        if (now - mgr->sessions[i]->last_activity > timeout_seconds) {
            printf("Removing idle session: %s\n", mgr->sessions[i]->session_id);
            free(mgr->sessions[i]);
            
            for (int j = i; j < mgr->session_count - 1; j++) {
                mgr->sessions[j] = mgr->sessions[j + 1];
            }
            mgr->session_count--;
            removed++;
        }
    }
    
    pthread_mutex_unlock(&mgr->lock);
    return removed;
}

// Destroy session manager
void session_manager_destroy(SessionManager* mgr) {
    pthread_mutex_lock(&mgr->lock);
    
    for (int i = 0; i < mgr->session_count; i++) {
        if (mgr->sessions[i]->user_data) {
            free(mgr->sessions[i]->user_data);
        }
        free(mgr->sessions[i]);
    }
    free(mgr->sessions);
    
    pthread_mutex_unlock(&mgr->lock);
    pthread_mutex_destroy(&mgr->lock);
    free(mgr);
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use std::time::{SystemTime, UNIX_EPOCH};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct ClientSession {
    pub session_id: String,
    pub username: Option<String>,
    pub ip_address: String,
    pub connect_time: u64,
    pub last_activity: u64,
    pub socket_id: usize,
    pub authenticated: bool,
    pub role: String,
    pub messages_sent: u64,
    pub messages_received: u64,
    pub metadata: HashMap<String, String>,
}

impl ClientSession {
    pub fn new(socket_id: usize, ip_address: String) -> Self {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        Self {
            session_id: Uuid::new_v4().to_string(),
            username: None,
            ip_address,
            connect_time: now,
            last_activity: now,
            socket_id,
            authenticated: false,
            role: "guest".to_string(),
            messages_sent: 0,
            messages_received: 0,
            metadata: HashMap::new(),
        }
    }
    
    pub fn authenticate(&mut self, username: String, role: String) {
        self.username = Some(username);
        self.role = role;
        self.authenticated = true;
        self.update_activity();
    }
    
    pub fn update_activity(&mut self) {
        self.last_activity = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
    }
    
    pub fn increment_sent(&mut self) {
        self.messages_sent += 1;
        self.update_activity();
    }
    
    pub fn increment_received(&mut self) {
        self.messages_received += 1;
        self.update_activity();
    }
    
    pub fn is_idle(&self, timeout_seconds: u64) -> bool {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        now - self.last_activity > timeout_seconds
    }
    
    pub fn session_duration(&self) -> u64 {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        now - self.connect_time
    }
}

#[derive(Clone)]
pub struct SessionManager {
    sessions: Arc<RwLock<HashMap<String, ClientSession>>>,
    socket_to_session: Arc<RwLock<HashMap<usize, String>>>,
}

impl SessionManager {
    pub fn new() -> Self {
        Self {
            sessions: Arc::new(RwLock::new(HashMap::new())),
            socket_to_session: Arc::new(RwLock::new(HashMap::new())),
        }
    }
    
    pub fn add_session(&self, socket_id: usize, ip_address: String) -> String {
        let session = ClientSession::new(socket_id, ip_address);
        let session_id = session.session_id.clone();
        
        let mut sessions = self.sessions.write().unwrap();
        let mut socket_map = self.socket_to_session.write().unwrap();
        
        sessions.insert(session_id.clone(), session);
        socket_map.insert(socket_id, session_id.clone());
        
        println!("Session created: {} (socket: {})", session_id, socket_id);
        session_id
    }
    
    pub fn get_session(&self, session_id: &str) -> Option<ClientSession> {
        let sessions = self.sessions.read().unwrap();
        sessions.get(session_id).cloned()
    }
    
    pub fn get_session_by_socket(&self, socket_id: usize) -> Option<ClientSession> {
        let socket_map = self.socket_to_session.read().unwrap();
        if let Some(session_id) = socket_map.get(&socket_id) {
            let sessions = self.sessions.read().unwrap();
            sessions.get(session_id).cloned()
        } else {
            None
        }
    }
    
    pub fn update_session<F>(&self, session_id: &str, update_fn: F) -> bool
    where
        F: FnOnce(&mut ClientSession),
    {
        let mut sessions = self.sessions.write().unwrap();
        if let Some(session) = sessions.get_mut(session_id) {
            update_fn(session);
            true
        } else {
            false
        }
    }
    
    pub fn authenticate_session(
        &self,
        session_id: &str,
        username: String,
        role: String,
    ) -> bool {
        self.update_session(session_id, |session| {
            session.authenticate(username.clone(), role.clone());
            println!("Session {} authenticated as {} (role: {})", 
                     session_id, username, role);
        })
    }
    
    pub fn remove_session(&self, session_id: &str) -> bool {
        let mut sessions = self.sessions.write().unwrap();
        let mut socket_map = self.socket_to_session.write().unwrap();
        
        if let Some(session) = sessions.remove(session_id) {
            socket_map.remove(&session.socket_id);
            println!(
                "Session removed: {} (user: {:?}, duration: {}s)",
                session_id,
                session.username,
                session.session_duration()
            );
            true
        } else {
            false
        }
    }
    
    pub fn remove_session_by_socket(&self, socket_id: usize) -> bool {
        let socket_map = self.socket_to_session.read().unwrap();
        if let Some(session_id) = socket_map.get(&socket_id) {
            let session_id = session_id.clone();
            drop(socket_map);
            self.remove_session(&session_id)
        } else {
            false
        }
    }
    
    pub fn get_sessions_by_username(&self, username: &str) -> Vec<ClientSession> {
        let sessions = self.sessions.read().unwrap();
        sessions
            .values()
            .filter(|s| {
                s.username.as_ref().map(|u| u.as_str()) == Some(username)
            })
            .cloned()
            .collect()
    }
    
    pub fn get_all_sessions(&self) -> Vec<ClientSession> {
        let sessions = self.sessions.read().unwrap();
        sessions.values().cloned().collect()
    }
    
    pub fn session_count(&self) -> usize {
        let sessions = self.sessions.read().unwrap();
        sessions.len()
    }
    
    pub fn authenticated_count(&self) -> usize {
        let sessions = self.sessions.read().unwrap();
        sessions.values().filter(|s| s.authenticated).count()
    }
    
    pub fn cleanup_idle_sessions(&self, timeout_seconds: u64) -> usize {
        let sessions = self.sessions.read().unwrap();
        let idle_sessions: Vec<String> = sessions
            .values()
            .filter(|s| s.is_idle(timeout_seconds))
            .map(|s| s.session_id.clone())
            .collect();
        drop(sessions);
        
        let count = idle_sessions.len();
        for session_id in idle_sessions {
            self.remove_session(&session_id);
        }
        
        if count > 0 {
            println!("Cleaned up {} idle sessions", count);
        }
        count
    }
    
    pub fn print_statistics(&self) {
        let sessions = self.sessions.read().unwrap();
        let total = sessions.len();
        let authenticated = sessions.values().filter(|s| s.authenticated).count();
        
        println!("\n=== Session Statistics ===");
        println!("Total active sessions: {}", total);
        println!("Authenticated sessions: {}", authenticated);
        println!("Guest sessions: {}", total - authenticated);
        
        let total_messages: u64 = sessions.values()
            .map(|s| s.messages_sent + s.messages_received)
            .sum();
        println!("Total messages: {}", total_messages);
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;
    use std::time::Duration;
    
    #[test]
    fn test_session_management() {
        let manager = SessionManager::new();
        
        // Add sessions
        let session_id1 = manager.add_session(1, "192.168.1.100".to_string());
        let session_id2 = manager.add_session(2, "192.168.1.101".to_string());
        
        assert_eq!(manager.session_count(), 2);
        
        // Authenticate a session
        manager.authenticate_session(
            &session_id1,
            "alice".to_string(),
            "admin".to_string(),
        );
        
        assert_eq!(manager.authenticated_count(), 1);
        
        // Update activity
        manager.update_session(&session_id1, |session| {
            session.increment_sent();
        });
        
        // Get session by username
        let alice_sessions = manager.get_sessions_by_username("alice");
        assert_eq!(alice_sessions.len(), 1);
        assert_eq!(alice_sessions[0].messages_sent, 1);
        
        // Test idle cleanup
        thread::sleep(Duration::from_secs(2));
        let removed = manager.cleanup_idle_sessions(1);
        assert_eq!(removed, 1); // session_id2 should be removed as idle
        
        manager.print_statistics();
    }
}
```

## Summary

**Client Session Management** is fundamental to building robust WebSocket applications. It provides the infrastructure to:

- **Track Individual Clients**: Maintain unique identifiers and state for each connection
- **Store Metadata**: Keep relevant information (authentication, preferences, statistics) accessible per client
- **Manage Lifecycles**: Handle connection establishment, maintenance, and cleanup efficiently
- **Enable Features**: Support authentication, authorization, rate limiting, and personalized experiences
- **Monitor Performance**: Track metrics per session for debugging and optimization

Both implementations demonstrate essential patterns: thread-safe concurrent access (mutex in C, RwLock in Rust), efficient lookup mechanisms (arrays/hash maps), session lifecycle management, and metadata tracking. The Rust version leverages type safety and ownership to prevent common concurrency bugs, while the C version provides fine-grained control with manual memory management. Proper session management is critical for scalability, security, and providing stateful experiences in real-time WebSocket applications.