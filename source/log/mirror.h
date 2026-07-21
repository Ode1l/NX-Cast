#ifndef NXCAST_LOG_MIRROR_H
#define NXCAST_LOG_MIRROR_H

#include <stdbool.h>

typedef enum
{
    LOG_MIRROR_WRITE_OK = 0,
    LOG_MIRROR_WRITE_DROPPED,
    LOG_MIRROR_WRITE_FAILED
} LogMirrorWriteResult;

bool log_mirror_configure_nonblocking(int socket_fd);
LogMirrorWriteResult log_mirror_write_nonblocking(int socket_fd,
                                                   const char *line);

#endif
