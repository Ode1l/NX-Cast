#pragma once

#include <stdbool.h>
#include <stddef.h>

// Initialize SOAP control module runtime state.
bool soap_server_start(void);

// Shutdown SOAP control module runtime state.
void soap_server_stop(void);

// Try handling an HTTP request for DLNA SOAP control.
// Returns true if the request path belongs to SOAP control and a response was built.
bool soap_server_try_handle_http(const char *method,
                          const char *path,
                          const char *request,
                          size_t request_len,
                          char *response,
                          size_t response_size,
                          size_t *response_len);
