#include "mirror.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

bool log_mirror_configure_nonblocking(int socket_fd)
{
    int flags;

    if (socket_fd < 0)
        return false;
    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    if ((flags & O_NONBLOCK) != 0)
        return true;
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

LogMirrorWriteResult log_mirror_write_nonblocking(int socket_fd,
                                                   const char *line)
{
    char *record;
    ssize_t sent;
    size_t length;
    int flags = MSG_DONTWAIT;

    if (socket_fd < 0 || !line)
        return LOG_MIRROR_WRITE_FAILED;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    length = strlen(line);
    if (length == SIZE_MAX)
        return LOG_MIRROR_WRITE_FAILED;
    record = malloc(length + 1u);
    if (!record)
        return LOG_MIRROR_WRITE_DROPPED;
    memcpy(record, line, length);
    record[length] = '\n';
    sent = send(socket_fd, record, length + 1u, flags);
    free(record);
    if (sent == (ssize_t)(length + 1u))
        return LOG_MIRROR_WRITE_OK;
    if (sent >= 0)
        return LOG_MIRROR_WRITE_DROPPED;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ||
        errno == ENOBUFS || errno == ENOMEM)
        return LOG_MIRROR_WRITE_DROPPED;
    return LOG_MIRROR_WRITE_FAILED;
}
