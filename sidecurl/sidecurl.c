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

#define QUACK_SIZE 2048
static char LAST_QUACK[2 * QUACK_SIZE]; /* 2x for good luck */

char ERROR_BUFFER[CURL_ERROR_SIZE];

static char *BINARY_NAME = "sidecurl";
static char *URL, *WRITE_AFTER, *SIDEKICK_QUACK_STYLE;
static struct curl_slist *HEADERS = NULL;
static FILE *BODY_INPUT_FILE;
static FILE *OUTPUT_FILE;
static int QUICHE_MIN_ACK_DELAY, QUICHE_MAX_ACK_DELAY, SIDEKICK_MTU = 1;
static int SIDEKICK_THRESHOLD, SIDEKICK_MARK_ACKED = 0,
           SIDEKICK_MARK_LOST_AND_RETX = 1, SIDEKICK_UPDATE_CWND = 1,
           SIDEKICK_NEAR_DELAY, SIDEKICK_E2E_DELAY;
static int SIDEKICK_RESET = 1, SIDEKICK_RESET_PORT,
           SIDEKICK_RESET_THRESHOLD;
static int SIDEKICK_REORDER_THRESHOLD;
static int INSECURE, VERBOSE;
static long HTTP_VERSION = CURL_HTTP_VERSION_NONE;
static double TIMEOUT_SECS;
static void parseargs(int argc, char **argv);
void ourWriteOut(const char *writeinfo, CURL *easy, CURLcode per_result);
void quiche_conn_recv_quack(void *conn, uint8_t *quack_buf, size_t quack_buf_len,
    const struct sockaddr *addr, size_t addr_len);

int main(int argc, char **argv) {
    parseargs(argc, argv);
    int use_sidekick = SIDEKICK_THRESHOLD > 0;

    /*** OPEN A SIDEKICK SOCKET ***/
    ssize_t n_bytes_quacked;
    int sidekick_socket;
    if (use_sidekick) {
        sidekick_socket = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5103);  // listen for quacks on port 5103
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        assert(!bind(sidekick_socket, (struct sockaddr *)&addr, sizeof(addr)));
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
    checkok(curl_easy_setopt(easy_handle, CURLOPT_HTTPHEADER, HEADERS));
    // Set sidekick options
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_THRESHOLD, SIDEKICK_THRESHOLD));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_MARK_ACKED, SIDEKICK_MARK_ACKED));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_MARK_LOST_AND_RETX, SIDEKICK_MARK_LOST_AND_RETX));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_UPDATE_CWND, SIDEKICK_UPDATE_CWND));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_NEAR_DELAY, SIDEKICK_NEAR_DELAY));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_E2E_DELAY, SIDEKICK_E2E_DELAY));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_RESET, SIDEKICK_RESET));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_RESET_PORT, SIDEKICK_RESET_PORT));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_RESET_THRESHOLD, SIDEKICK_RESET_THRESHOLD));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_REORDER_THRESHOLD, SIDEKICK_REORDER_THRESHOLD));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_QUACK_STYLE, SIDEKICK_QUACK_STYLE));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDEKICK_MTU, SIDEKICK_MTU));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_QUICHE_MIN_ACK_DELAY, QUICHE_MIN_ACK_DELAY));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_QUICHE_MAX_ACK_DELAY, QUICHE_MAX_ACK_DELAY));

    CURLM *multi_handle = curl_multi_init();
    checkok(curl_multi_add_handle(multi_handle, easy_handle));

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000;

    fd_set fdread, fdwrite, fdexcep;
    int n_transfers_running;
    struct sockaddr from_addr;
    socklen_t from_addr_len;
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
        if (use_sidekick) {
            assert(sidekick_socket < FD_SETSIZE);
            FD_SET(sidekick_socket, &fdread);
        }

        /*** SELECT ***/
        select(maxfd + 2, &fdread, &fdwrite, &fdexcep, &timeout);
        if (use_sidekick && FD_ISSET(sidekick_socket, &fdread)) {
            from_addr_len = sizeof(from_addr);
            n_bytes_quacked = recvfrom(sidekick_socket, LAST_QUACK,
                                       QUACK_SIZE, MSG_DONTWAIT,
                                       &from_addr, &from_addr_len);
            if (n_bytes_quacked > 0) {
                LAST_QUACK[n_bytes_quacked] = '\0';
                void *conn = NULL;
                checkok(curl_easy_getinfo(easy_handle, CURLINFO_QUICHE_CONN, &conn));
                #ifdef DEBUG
                printf("New quack: '%s'\n", LAST_QUACK);
                #endif
                quiche_conn_recv_quack(conn, LAST_QUACK, n_bytes_quacked,
                    &from_addr, from_addr_len);

                if (multi_handle)
                    curl_multi_quic_flush_egress(multi_handle);
            } else if (n_bytes_quacked < 0 && errno != EAGAIN) {
                perror("Error getting quack:");
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

    if (HEADERS) {
        curl_slist_free_all(HEADERS);
    }
    return 0;
}

void usage() {
    fprintf(stderr, "Usage: %s <args> [URL]\n", BINARY_NAME);
    fprintf(stderr, "Options:\n"
"-o, --output <file>         write to <file> instead of stdout\n"
"-1, --http1.1               tell curl to use HTTP v1.1\n"
"-3, --http3                 tell curl to use HTTP v3\n"
"    --sidekick <threshold>       enable the sidekick and set the power sum quACK threshold\n"
"    --mark-acked <bool>          use quacks to consider packets received (default: 0)\n"
"    --mark-lost-and-retx <bool>  use quacks to consider packets lost, and retransmit (default: 1)\n"
"    --update-cwnd <bool>         use quacks to update the cwnd on loss (default: 1)\n"
"    --near-delay <ms>            set the estimated delay between the sender and proxy, in ms (default: 1)\n"
"    --e2e-delay <ms>             set the estimated delay between the proxy and receiver, in ms (default: 26)\n"
"    --enable-reset <bool>        whether to send sidekick reset messages (default: 1)\n"
"    --reset-port <port>          port to send sidekick reset messages to (default: 1234)\n"
"    --reset-threshold <ms>       threshold that determines frequency of sidekick reset messages (default: 10)\n"
"    --reorder-threshold <pkts>   threshold for sidekick loss detection (default: 3)\n"
"-u, --quack-style <style>        style of quack to send/receive\n"
"    --disable-mtu-fix            disable fix that sends packets only if the cwnd > mtu\n"
"-M, --min-ack-delay <ms>         minimum delay between acks, in ms\n"
"-D, --max-ack-delay <ms>         maximum delay between acks, in ms\n"
"    --header header         extra header to include in information sent\n"
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
        {"header",      required_argument, 0, 'H'},
        // SIDEKICK-SPECIFIC OPTIONS
        {"sidekick",           required_argument, 0, 'a'},
        {"mark-acked",         required_argument, 0, 'b'},
        {"mark-lost-and-retx", required_argument, 0, 'c'},
        {"update-cwnd",        required_argument, 0, 'e'},
        {"near-delay",         required_argument, 0, 'f'},
        {"e2e-delay",          required_argument, 0, 'g'},
        {"enable-reset",       required_argument, 0, 'h'},
        {"reset-port",         required_argument, 0, 'i'},
        {"reset-threshold",    required_argument, 0, 'l'},
        {"reorder-threshold",  required_argument, 0, 'n'},
        {"quack-style",        required_argument, 0, 'p'},
        {"disable-mtu-fix",    no_argument,       0, 'q'},
        {"min-ack-delay",      required_argument, 0, 'r'},
        {"max-ack-delay",      required_argument, 0, 's'},
        {0, 0, 0, 0},
    };
    while (1) {
        int c = getopt_long(argc, argv, "o:w:d:m:k:1:3:v:H:q:s:t:Q:S:M:D", options, NULL);
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
        case '1': HTTP_VERSION = CURL_HTTP_VERSION_1_1; break;
        case '3': HTTP_VERSION = CURL_HTTP_VERSION_3; break;
        case 'v': VERBOSE = 1; break;
        case 'H': HEADERS = curl_slist_append(HEADERS, strdup(optarg)); break;
        case 'a': SIDEKICK_THRESHOLD = atoi(optarg); break;
        case 'b': SIDEKICK_MARK_ACKED = atoi(optarg); break;
        case 'c': SIDEKICK_MARK_LOST_AND_RETX = atoi(optarg); break;
        case 'e': SIDEKICK_UPDATE_CWND = atoi(optarg); break;
        case 'f': SIDEKICK_NEAR_DELAY = atoi(optarg); break;
        case 'g': SIDEKICK_E2E_DELAY = atoi(optarg); break;
        case 'h': SIDEKICK_RESET = atoi(optarg); break;
        case 'i': SIDEKICK_RESET_PORT = atoi(optarg); break;
        case 'l': SIDEKICK_RESET_THRESHOLD = atoi(optarg); break;
        case 'n': SIDEKICK_REORDER_THRESHOLD = atoi(optarg); break;
        case 'p': SIDEKICK_QUACK_STYLE = strdup(optarg); break;
        case 'q': SIDEKICK_MTU = 0; break;
        case 'r': QUICHE_MIN_ACK_DELAY = atoi(optarg); break;
        case 's': QUICHE_MAX_ACK_DELAY = atoi(optarg); break;
        case '?': usage();
        }
    }
    if ((optind + 1) != argc) usage();
    URL = argv[optind];
}
