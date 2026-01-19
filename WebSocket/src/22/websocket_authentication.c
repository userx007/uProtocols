#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define MAX_CLIENTS 100
#define TOKEN_EXPIRY 3600  // 1 hour in seconds

// Client session structure
typedef struct {
    struct lws *wsi;
    char user_id[64];
    char token[256];
    time_t token_expiry;
    int authenticated;
    char roles[128];  // Comma-separated roles
} client_session_t;

// Global session store
client_session_t sessions[MAX_CLIENTS];
int session_count = 0;

// Simple JWT validation (simplified - use a proper JWT library in production)
int validate_jwt_token(const char *token, char *user_id, char *roles) {
    // In production, use a proper JWT library like libjwt
    // This is a simplified example showing the concept
    
    // Split token into header.payload.signature
    char token_copy[256];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    
    char *header = strtok(token_copy, ".");
    char *payload = strtok(NULL, ".");
    char *signature = strtok(NULL, ".");
    
    if (!header || !payload || !signature) {
        return 0;
    }
    
    // Verify signature (simplified)
    const char *secret = "your-secret-key";
    unsigned char hmac_result[EVP_MAX_MD_SIZE];
    unsigned int hmac_len;
    
    char data_to_sign[512];
    snprintf(data_to_sign, sizeof(data_to_sign), "%s.%s", header, payload);
    
    HMAC(EVP_sha256(), secret, strlen(secret),
         (unsigned char*)data_to_sign, strlen(data_to_sign),
         hmac_result, &hmac_len);
    
    // Base64 decode signature and compare (simplified)
    // In production, properly decode and compare
    
    // Decode payload (base64 decode, then parse JSON)
    // This is simplified - use a proper base64 decoder and JSON parser
    strcpy(user_id, "user123");  // Extract from decoded payload
    strcpy(roles, "admin,user");  // Extract from decoded payload
    
    return 1;  // Token is valid
}

// Check if user has required role
int has_role(const char *user_roles, const char *required_role) {
    if (!user_roles || !required_role) return 0;
    return strstr(user_roles, required_role) != NULL;
}

// Find or create session for connection
client_session_t* get_session(struct lws *wsi) {
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].wsi == wsi) {
            return &sessions[i];
        }
    }
    
    // Create new session
    if (session_count < MAX_CLIENTS) {
        sessions[session_count].wsi = wsi;
        sessions[session_count].authenticated = 0;
        return &sessions[session_count++];
    }
    
    return NULL;
}

// Remove session
void remove_session(struct lws *wsi) {
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].wsi == wsi) {
            // Shift remaining sessions
            for (int j = i; j < session_count - 1; j++) {
                sessions[j] = sessions[j + 1];
            }
            session_count--;
            break;
        }
    }
}

// WebSocket protocol callback
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    client_session_t *session;
    
    switch (reason) {
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            // Authentication during handshake
            char token[256] = {0};
            
            // Try to get token from query parameter
            char name[32], value[256];
            int n = 0;
            while (lws_hdr_copy_fragment(wsi, value, sizeof(value),
                                         WSI_TOKEN_HTTP_URI_ARGS, n) > 0) {
                if (sscanf(value, "token=%255s", token) == 1) {
                    break;
                }
                n++;
            }
            
            // Or from Authorization header
            if (token[0] == '\0') {
                if (lws_hdr_copy(wsi, token, sizeof(token),
                                WSI_TOKEN_HTTP_AUTHORIZATION) > 0) {
                    // Remove "Bearer " prefix if present
                    char *bearer = strstr(token, "Bearer ");
                    if (bearer) {
                        memmove(token, bearer + 7, strlen(bearer + 7) + 1);
                    }
                }
            }
            
            // Validate token
            char user_id[64], roles[128];
            if (token[0] != '\0' && validate_jwt_token(token, user_id, roles)) {
                // Token valid at handshake - mark for post-connection auth
                lwsl_user("Valid token presented at handshake for user: %s\n", user_id);
                return 0;  // Accept connection
            }
            
            // Can still accept and require auth after connection
            lwsl_user("No valid token at handshake, will require auth message\n");
            return 0;
        }
        
        case LWS_CALLBACK_ESTABLISHED: {
            session = get_session(wsi);
            if (!session) {
                lwsl_err("Failed to create session\n");
                return -1;
            }
            
            lwsl_user("WebSocket connection established\n");
            
            // Send authentication request if not authenticated
            const char *auth_req = "{\"type\":\"auth_required\"}";
            unsigned char buf[LWS_PRE + 256];
            memcpy(&buf[LWS_PRE], auth_req, strlen(auth_req));
            lws_write(wsi, &buf[LWS_PRE], strlen(auth_req), LWS_WRITE_TEXT);
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            session = get_session(wsi);
            if (!session) return -1;
            
            char *message = (char *)in;
            
            // Parse message (simplified - use JSON parser in production)
            if (!session->authenticated) {
                // Expect authentication message
                char token[256] = {0};
                if (sscanf(message, "{\"type\":\"auth\",\"token\":\"%255[^\"]\"}", token) == 1) {
                    char user_id[64], roles[128];
                    
                    if (validate_jwt_token(token, user_id, roles)) {
                        // Authentication successful
                        session->authenticated = 1;
                        strncpy(session->user_id, user_id, sizeof(session->user_id) - 1);
                        strncpy(session->token, token, sizeof(session->token) - 1);
                        strncpy(session->roles, roles, sizeof(session->roles) - 1);
                        session->token_expiry = time(NULL) + TOKEN_EXPIRY;
                        
                        lwsl_user("User %s authenticated with roles: %s\n", 
                                 user_id, roles);
                        
                        const char *resp = "{\"type\":\"auth_success\"}";
                        unsigned char buf[LWS_PRE + 256];
                        memcpy(&buf[LWS_PRE], resp, strlen(resp));
                        lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                    } else {
                        const char *resp = "{\"type\":\"auth_failed\"}";
                        unsigned char buf[LWS_PRE + 256];
                        memcpy(&buf[LWS_PRE], resp, strlen(resp));
                        lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                        return -1;  // Close connection
                    }
                }
            } else {
                // Check token expiry
                if (time(NULL) > session->token_expiry) {
                    lwsl_user("Token expired for user %s\n", session->user_id);
                    const char *resp = "{\"type\":\"token_expired\"}";
                    unsigned char buf[LWS_PRE + 256];
                    memcpy(&buf[LWS_PRE], resp, strlen(resp));
                    lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                    return -1;
                }
                
                // Authorization check - example for admin-only action
                char action[64] = {0};
                if (sscanf(message, "{\"type\":\"%63[^\"]\"}", action) == 1) {
                    if (strcmp(action, "admin_action") == 0) {
                        if (!has_role(session->roles, "admin")) {
                            lwsl_user("Unauthorized: user %s attempted admin action\n",
                                     session->user_id);
                            const char *resp = "{\"type\":\"unauthorized\"}";
                            unsigned char buf[LWS_PRE + 256];
                            memcpy(&buf[LWS_PRE], resp, strlen(resp));
                            lws_write(wsi, &buf[LWS_PRE], strlen(resp), LWS_WRITE_TEXT);
                            return 0;
                        }
                        
                        lwsl_user("User %s performed admin action\n", session->user_id);
                        // Process admin action...
                    }
                }
            }
            break;
        }
        
        case LWS_CALLBACK_CLOSED: {
            remove_session(wsi);
            lwsl_user("Connection closed\n");
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "websocket-protocol",
        callback_websocket,
        0,
        4096,
    },
    { NULL, NULL, 0, 0 } /* terminator */
};

int main(void) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    
    memset(&info, 0, sizeof(info));
    info.port = 8080;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return -1;
    }
    
    lwsl_user("WebSocket server started on port 8080\n");
    
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}