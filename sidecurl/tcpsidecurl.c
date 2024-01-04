#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "curl/curl.h"

#define checkok(x) do { \
    CURLcode err = (x); \
    if (err != CURLE_OK) { \
        fprintf(stderr, "CURL Error on line %d: %s\n", __LINE__, ERROR_BUFFER); \
        exit(1); \
    } \
} while (0)

#define QUACK_SIZE 4
static char LAST_QUACK[4 * QUACK_SIZE]; /* 4x for good luck */

char ERROR_BUFFER[CURL_ERROR_SIZE];

static char *BINARY_NAME = "sidecurl";
static char *URL, *WRITE_AFTER, *SIDECAR_QUACK_STYLE;
static FILE *BODY_INPUT_FILE;
static FILE *OUTPUT_FILE;
static int QUICHE_MIN_ACK_DELAY, QUICHE_MAX_ACK_DELAY;
static int SIDECAR_THRESHOLD, SIDECAR_QUACK_RESET, SIDECAR_MTU, INSECURE, VERBOSE;
static long HTTP_VERSION = CURL_HTTP_VERSION_NONE;
static double TIMEOUT_SECS;
static void parseargs(int argc, char **argv);
void ourWriteOut(const char *writeinfo, CURL *easy, CURLcode per_result);
void quiche_conn_recv_quack(void *conn, uint8_t *quack_buf, size_t quack_buf_len,
    const struct sockaddr *addr, size_t addr_len);

int main(int argc, char **argv) {
    parseargs(argc, argv);
    int use_sidecar = SIDECAR_THRESHOLD > 0;

    /*** OPEN A SIDECAR SOCKET ***/
    ssize_t n_bytes_quacked, n_bytes_in_buffer = 0;
    int sidecar_socket, sidecar_fd;
    struct sockaddr from_addr;
    socklen_t from_addr_len = sizeof(from_addr);
    if (use_sidecar) {
        sidecar_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5103);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        assert(!bind(sidecar_socket, (struct sockaddr *)&addr, sizeof(addr)));
        assert(!listen(sidecar_socket, 1));
        assert(0 <= (sidecar_fd = accept(sidecar_socket, &from_addr, &from_addr_len)));
    }

    /*** OPEN A CURL HANDLE ***/
    CURL *easy_handle = curl_easy_init();
    // Set cURL options
    checkok(curl_easy_setopt(easy_handle, CURLOPT_ERRORBUFFER, ERROR_BUFFER));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_URL, URL));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_HTTP_VERSION, HTTP_VERSION));
    if (TIMEOUT_SECS > 0.)
        checkok(curl_easy_setopt(easy_handle, CURLOPT_TIMEOUT_MS,
                                 (long)(TIMEOUT_SECS * 1000)));
    if (BODY_INPUT_FILE) {
        checkok(curl_easy_setopt(easy_handle, CURLOPT_POST, 1L));
        checkok(curl_easy_setopt(easy_handle, CURLOPT_READDATA, BODY_INPUT_FILE));
    }
    if (OUTPUT_FILE)
        checkok(curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, OUTPUT_FILE));
    if (INSECURE) {
        checkok(curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYPEER, 0L));
        checkok(curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYHOST, 0L));
    }
    checkok(curl_easy_setopt(easy_handle, CURLOPT_VERBOSE, VERBOSE));
    // Set sidecar options
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDECAR_THRESHOLD, SIDECAR_THRESHOLD));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDECAR_QUACK_RESET, SIDECAR_QUACK_RESET));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDECAR_MTU, SIDECAR_MTU));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDECAR_QUACK_STYLE, SIDECAR_QUACK_STYLE));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_QUICHE_MIN_ACK_DELAY, QUICHE_MIN_ACK_DELAY));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_QUICHE_MAX_ACK_DELAY, QUICHE_MAX_ACK_DELAY));

    CURLM *multi_handle = curl_multi_init();
    checkok(curl_multi_add_handle(multi_handle, easy_handle));

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000;

    fd_set fdread, fdwrite, fdexcep;
    int n_transfers_running;
    do {
        /*** FILE DESCRIPTORS ***/
        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);
        // Get from cURL
        int maxfd = -1;
        while (maxfd == -1) {
            checkok(curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd));
            if (maxfd != -1) break;
            curl_multi_perform(multi_handle, &n_transfers_running);
        }
        // Then add ours
        if (use_sidecar) {
            assert(sidecar_fd < FD_SETSIZE);
            FD_SET(sidecar_fd, &fdread);
        }

        /*** SELECT ***/
        select(maxfd + 2, &fdread, &fdwrite, &fdexcep, &timeout);
        if (use_sidecar && FD_ISSET(sidecar_fd, &fdread)) {
            n_bytes_quacked = read(
                sidecar_fd, LAST_QUACK + n_bytes_in_buffer,
                QUACK_SIZE - n_bytes_in_buffer);
            if (n_bytes_quacked > 0) {
                n_bytes_in_buffer += n_bytes_quacked;
                if (n_bytes_in_buffer != QUACK_SIZE) continue;

                void *conn = NULL;
                checkok(curl_easy_getinfo(easy_handle, CURLINFO_QUICHE_CONN, &conn));
                // printf("New quack: '%s'\n", LAST_QUACK);
                quiche_conn_recv_quack(conn, LAST_QUACK, n_bytes_quacked,
                    &from_addr, from_addr_len);

                n_bytes_in_buffer = 0;

                if (multi_handle)
                    curl_multi_quic_flush_egress(multi_handle);
            } else if (n_bytes_quacked < 0 && errno != EAGAIN) {
                perror("Error getting quack");
                exit(1);
            }
        } else {
            curl_multi_perform(multi_handle, &n_transfers_running);
        }
    } while (n_transfers_running);

    if (WRITE_AFTER) {
        int msgq = 0;
        CURLMsg *msg = curl_multi_info_read(multi_handle, &msgq);
        CURLcode result = CURLE_OK;
        if (msgq == 0) {
            result = msg->data.result;
        } else fprintf(stderr, "Weird result from multi_info_read...\n");
        ourWriteOut(WRITE_AFTER, easy_handle, result);
    }

    return 0;
}

void usage() {
    fprintf(stderr, "Usage: %s <args> [URL]\n", BINARY_NAME);
    fprintf(stderr, "Options:\n"
"-o, --output <file>         write to <file> instead of stdout\n"
"-1, --http1.1               tell curl to use HTTP v1.1\n"
"-3, --http3                 tell curl to use HTTP v3\n"
"-Q, --quack-reset           whether to send quack reset messages\n"
"-u, --quack-style           style of quack to send/receive\n"
"-S, --sidecar-mtu           send packets only if the cwnd > mtu\n"
"-t, --threshold <number>    specify the sidecar threshold\n"
"-M, --min-ack-delay         minimum delay between acks, in ms\n"
"-D, --max-ack-delay         maximum delay between acks, in ms\n"
"-w, --write-out <format>    format string for display on stdout afterwards\n"
"-d, --data-binary @<file>   send the contents of @<file> as an HTTP POST\n"
"                            NOTE: only @<file> supported currently, not <str>\n"
"-k, --insecure              tell curl not to attempt to validate peer signatures\n"
"-m, --max-time <secs>       timeout the operation after this many seconds\n");
    exit(1);
}
void parseargs(int argc, char **argv) {
    if (argc == 0) usage();

    struct option options[] = {
        // GENERIC CURL OPTIONS
        {"output",      required_argument, 0, 'o'},
        {"write-out",   required_argument, 0, 'w'},
        {"data-binary", required_argument, 0, 'd'},
        {"max-time",    required_argument, 0, 'm'},
        {"insecure",    no_argument,       0, 'k'},
        {"http1.1",     no_argument,       0, '1'},
        {"http3",       no_argument,       0, '3'},
        {"verbose",     no_argument,       0, 'v'},
        // SIDECAR-SPECIFIC OPTIONS
        {"sidecar",     required_argument, 0, 's'},
        {"threshold",   required_argument, 0, 't'},
        {"quack-reset", no_argument,       0, 'Q'},
        {"sidecar-mtu", no_argument,       0, 'S'},
        {"min-ack-delay", required_argument, 0, 'M'},
        {"max-ack-delay", required_argument, 0, 'D'},
        {"quack-style",   required_argument, 0, 'u'},
        {0, 0, 0, 0},
    };
    while (1) {
        int c = getopt_long(argc, argv, "o:w:d:m:k13vq:s:t:", options, NULL);
        if (c == -1) break;
        switch (c) {
        case 'o':
            OUTPUT_FILE = fopen(optarg, "w");
            if (!OUTPUT_FILE) {
                printf("Error opening output file '%s'\n", optarg);
                perror("Error Message:");
            }
            break;
        case 'd':
            assert(optarg[0] == '@');
            BODY_INPUT_FILE = fopen(optarg + 1, "r");
            if (!BODY_INPUT_FILE) {
                printf("Error opening POST body file '%s'\n", optarg + 1);
                perror("Error Message:");
            }
            break;
        case 'w': WRITE_AFTER = strdup(optarg); break;
        case 'm': TIMEOUT_SECS = atof(optarg); break;
        case 'k': INSECURE = 1; break;
        case 'v': VERBOSE = 1; break;
        case '1': HTTP_VERSION = CURL_HTTP_VERSION_1_1; break;
        case '3': HTTP_VERSION = CURL_HTTP_VERSION_3; break;
        case 't': SIDECAR_THRESHOLD = atoi(optarg); break;
        case 'Q': SIDECAR_QUACK_RESET = 1; break;
        case 'S': SIDECAR_MTU = 1; break;
        case 'u': SIDECAR_QUACK_STYLE = strdup(optarg); break;
        case 'M': QUICHE_MIN_ACK_DELAY = atoi(optarg); break;
        case 'D': QUICHE_MAX_ACK_DELAY = atoi(optarg); break;
        case '?': usage();
        }
    }
    if ((optind + 1) != argc) usage();
    URL = argv[optind];
}
