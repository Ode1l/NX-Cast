#include "log/mirror.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_success(void)
{
    int sockets[2];
    char buffer[32] = {0};
    size_t received = 0u;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(log_mirror_write_nonblocking(sockets[0], "health") ==
           LOG_MIRROR_WRITE_OK);
    while (received < 7u)
    {
        ssize_t result = read(sockets[1], buffer + received, 7u - received);

        assert(result > 0);
        received += (size_t)result;
    }
    assert(strcmp(buffer, "health\n") == 0);
    close(sockets[0]);
    close(sockets[1]);
}

static void test_backpressure_is_dropped(void)
{
    int sockets[2];
    char payload[4096];
    int send_buffer_size = 4096;
    ssize_t sent;

    memset(payload, 'x', sizeof(payload));
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(log_mirror_configure_nonblocking(sockets[0]));
    assert(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &send_buffer_size,
                      sizeof(send_buffer_size)) == 0);

    do
    {
        sent = send(sockets[0], payload, sizeof(payload), MSG_DONTWAIT);
    } while (sent > 0);

    assert(sent < 0);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    assert(log_mirror_write_nonblocking(sockets[0], "backpressure") ==
           LOG_MIRROR_WRITE_DROPPED);
    close(sockets[0]);
    close(sockets[1]);
}

static void test_backpressure_never_waits_for_peer(void)
{
    int sockets[2];
    char payload[4097];
    int send_buffer_size = 4096;
    unsigned int attempts = 0u;

    memset(payload, 'x', sizeof(payload) - 1u);
    payload[sizeof(payload) - 1u] = '\0';
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(log_mirror_configure_nonblocking(sockets[0]));
    assert(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &send_buffer_size,
                      sizeof(send_buffer_size)) == 0);

    while (attempts < 100000u)
    {
        LogMirrorWriteResult result =
            log_mirror_write_nonblocking(sockets[0], payload);

        if (result == LOG_MIRROR_WRITE_DROPPED)
            break;
        assert(result == LOG_MIRROR_WRITE_OK);
        attempts++;
    }

    assert(attempts < 100000u);
    close(sockets[0]);
    close(sockets[1]);
}

static void test_disconnected_peer_is_failed(void)
{
    int sockets[2];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    close(sockets[1]);
    assert(log_mirror_write_nonblocking(sockets[0], "closed") ==
           LOG_MIRROR_WRITE_FAILED);
    close(sockets[0]);
}

int main(void)
{
    (void)signal(SIGPIPE, SIG_IGN);
    (void)alarm(5u);
    test_success();
    test_backpressure_is_dropped();
    test_backpressure_never_waits_for_peer();
    test_disconnected_peer_is_failed();
    (void)alarm(0u);
    puts("log mirror tests passed");
    return 0;
}
