/**
 * @file A multi-threaded proxy with cache
 *
 * The proxy first listen for client connections, and forward any incoming
 * requests to the server. When doing so, it modifies the HTTP header:
 * - Add Host value
 * - Set User-Agent to Mozilla
 * - Set Connection and Proxy-Connection to close
 *
 * Then the proxy forward the HTTP response from server to the client.
 * If the response is very large, the proxy will repeated transmit the
 * response in chunks until all content is transmitted.
 *
 * The proxy will cache the response (if it's not too large) using a LRU cache.
 *
 * The proxy supports concurrent connections.
 *
 * @see cache.c
 * @see util.c
 */

#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "cache.h"
#include "debug.h"
#include "http_parser.h"
#include "util.h"

/// Max host string length
#define HOSTLEN 256

/// Max port string length
#define SERVLEN 8

/* Typedef for convenience */
typedef struct sockaddr SA;

/**
 * Information about a connected client
 */
typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

/**
 * Information about an HTTP request
 */
typedef struct {
    const char *method;  /// HTTP request method, e.g. GET or POST
    const char *version; /// The HTTP version without the HTTP/, e.g. 1.0 or 1.1
    const char *scheme;  /// scheme to connect over, e.g. http or https
    const char *uri;     /// the entire URI
    const char *host;    /// a network host, e.g. cs.cmu.edu
    const char *port;    /// The port to connect on, by default 80
    const char *path;    /// The path to find a resource, e.g. index.html
} http_info;

/**
 * String to use for the User-Agent header.
 * @note Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20220411 Firefox/63.0.1";

// function prototypes

static void *serve(void *vargp);
static void clienterror(int fd, const char *errnum, const char *shortmsg,
                        const char *longmsg);
static bool parse_http_request(int fd, parser_t *p, http_info *info);
static bool construct_new_request(parser_t *p, http_info info, char *out);
static bool forward_http_response(int host_fd, int client_fd,
                                  const char *cache_key);

// global variables

cache_t *g_cache = NULL;

/**
 * - Initialize
 * - Listen for connection
 * - Create a new thread for each connection
 * - Forward requests and responses in individual threads
 * - Threads are destroyed after the data is transmitted
 */
int main(int argc, char **argv) {
    /* Check command line args */
    if (argc != 2) {
        sio_eprintf("usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // ignore SIGPIPE
    Signal(SIGPIPE, SIG_IGN);

    int listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        sio_eprintf("Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }

    // init cache
    g_cache = cache_create();
    if (!g_cache) {
        sio_eprintf("Failed to initialize cache\n");
        exit(1);
    }

    // main loop
    while (1) {
        client_info *client = Malloc(sizeof(client_info));

        client->addrlen = sizeof(client->addr);

        // accept() will block until a client connects to the port
        client->connfd =
            accept(listenfd, (SA *)&client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, serve, client)) {
            sio_eprintf("pthread_create failed\n");
        }
    }

    return 0;
}

/**
 * Serve a client
 *
 * - Forward the HTTP request from client to server
 * - Forward the HTTP response from server to client
 *
 * Send an HTML error page and relevant HTTP status code to client if error
 * occurred
 */
static void *serve(void *vargp) {
    client_info client;
    memcpy(&client, vargp, sizeof(client));
    Free(vargp);

    if (pthread_detach(pthread_self())) {
        sio_eprintf("pthread_detach failed\n");
        return NULL;
    }

    parser_t *p = parser_new();
    http_info info;

    do { // easier control flow
        if (!parse_http_request(client.connfd, p, &info)) {
            break;
        }

#ifdef DEBUG
        // print client host and port
        int res = getnameinfo((SA *)&client.addr, client.addrlen, client.host,
                              sizeof(client.host), client.serv,
                              sizeof(client.serv), 0);
        if (!res) {
            sio_printf("Accepted connection from %s:%s, requesting %s\n",
                       client.host, client.serv, info.uri);
        }
#endif

        // skip contacting the server if URI is found in cache
        cache_entry_t *entry = cache_get(g_cache, info.uri);
        if (entry) {
            dbg_printf("Found cached HTTP response for %s\n", info.uri);
            if (rio_writen(client.connfd, entry->val, entry->size) < 0) {
                sio_eprintf("Failed to send cached HTTP response to client\n");
            }

            cache_entry_release(g_cache, entry);
            break;
        }

        // new request
        char new_req[MAXLINE];
        if (!construct_new_request(p, info, new_req)) {
            break;
        }

        // forward new request to the server
        int host_fd = open_clientfd(info.host, info.port);
        if (host_fd < 0) {
            sio_eprintf("Failed to connect to host: %s:%s\n", info.host,
                        info.port);
            break;
        }
        if (rio_writen(host_fd, new_req, strlen(new_req)) < 0) {
            sio_eprintf("Failed to forward request to: %s:%s\n", info.host,
                        info.port);
            break;
        }

        // forward server response to client
        if (!forward_http_response(host_fd, client.connfd, info.uri)) {
            sio_eprintf("Failed to forward HTTP response to client\n");
            break;
        }
    } while (false);

    // cleanup resources
    parser_free(p); // this will free the strings stored in info
    close(client.connfd);
    return NULL;
}

/**
 * Parse HTTP request
 * @param fd Socket descriptor
 * @param p HTTP parser
 * @param info HTTP info
 * @return false if an error occurred
 *
 * @note Send back an html file containing error details if necessary
 */
static bool parse_http_request(int fd, parser_t *p, http_info *info) {
    // read and parse all request lines
    char buf[MAXLINE];
    rio_t rio;
    rio_readinitb(&rio, fd);
    ssize_t n = 0; // total # of chars parsed
    while (true) {
        ssize_t len = rio_readlineb(&rio, buf, sizeof(buf));
        if (len < 0) {
            sio_eprintf(
                "rio_readlineb failed when reading request from client\n");
            return false;
        } else if (0 == len) { // EOF
            break;
        }
        // check for end of request headers
        else if (strcmp(buf, "\r\n") == 0) {
            break;
        }

        parser_state state = parser_parse_line(p, buf);
        if (state == ERROR) {
            sio_eprintf("Failed to parse HTTP request: %s\n", buf);
            break;
        }

        n += len;
    }

    if (!n) { // empty request
        return false;
    }

    // version must be either HTTP/1.0 or HTTP/1.1
    if (parser_retrieve(p, HTTP_VERSION, &info->version)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse HTTP version");
        return false;
    }
    if (!(strcmp(info->version, "1.0") == 0 ||
          strcmp(info->version, "1.1") == 0)) {
        clienterror(fd, "400", "Bad Request", "Wrong HTTP version");
        return false;
    }

    // method must be GET
    if (parser_retrieve(p, METHOD, &info->method)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse HTTP method");
        return false;
    }
    if (strcmp(info->method, "GET")) {
        clienterror(fd, "501", "Not Implemented",
                    "HTTP method not implemented");
        return false;
    }

    // scheme must be http
    if (parser_retrieve(p, SCHEME, &info->scheme)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse HTTP scheme");
        return false;
    }
    if (strcmp(info->scheme, "http")) {
        clienterror(fd, "501", "Not Implemented",
                    "HTTP scheme not implemented");
        return false;
    }

    // uri
    if (parser_retrieve(p, URI, &info->uri)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse uri");
        return false;
    }
    // host
    if (parser_retrieve(p, HOST, &info->host)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse host");
        return false;
    }
    // port
    if (parser_retrieve(p, PORT, &info->port)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse port");
        return false;
    }
    // path
    if (parser_retrieve(p, PATH, &info->path)) {
        clienterror(fd, "400", "Bad Request", "Cannot parse path");
        return false;
    }

    return true;
}

/**
 * Read HTTP request headers
 * @param p HTTP parser
 * @param info HTTP info
 * @param[out] out New request
 *
 * @note An empty line is appended denoting the end of the request
 * @return false if an error occurred
 */
static bool construct_new_request(parser_t *p, http_info info, char *out) {
    // TODO: check overflow for all snprintf calls

    int len = snprintf(out, MAXLINE, "GET %s HTTP/1.0\r\n", info.uri);
    out += len;

    // construct new headers
    bool host_found = false; // whether Host field is in the header
    while (true) {
        header_t *header = parser_retrieve_next_header(p);
        if (!header)
            break;

        // skip fields that are always overridden, and add them later
        if (strcmp(header->name, "Connection") == 0 ||
            strcmp(header->name, "Proxy-Connection") == 0 ||
            strcmp(header->name, "User-Agent") == 0) {
            continue;
        } else if (strcmp(header->name, "Host") == 0) {
            // check fields that are required but not overridden
            host_found = true;
        }

        len = snprintf(out, MAXLINE, "%s: %s\r\n", header->name, header->value);
        out += len;
    }

    // override or append some special fields
    if (!host_found) {
        len = snprintf(out, MAXLINE, "Host: %s:%s\r\n", info.host, info.port);
        out += len;
    }
    snprintf(out, MAXLINE,
             "Connection: close\r\nProxy-Connection: close\r\nUser-Agent: "
             "%s\r\n\r\n",
             header_user_agent);

    return true;
}

/**
 * Forward HTTP response from server to client.
 *
 * If the response is very large, the proxy will repeated transmit the
 * response in chunks until all content is transmitted.
 *
 * The proxy will cache the response (if it's not too large) using a LRU cache.
 *
 * @param host_fd Server socket descriptor
 * @param client_fd Server socket descriptor
 * @param cache_key String key (URI) used in the cache
 * @return false if an error occurred
 */
static bool forward_http_response(int host_fd, int client_fd,
                                  const char *cache_key) {
    // continuously read from host and write to client
    // only takes 1 iteration if the content size is small
    rio_t rio;
    rio_readinitb(&rio, host_fd);
    char buf[MAX_OBJECT_SIZE];
    size_t cache_size = 0;
    while (true) {
        // read from host
        ssize_t len = rio_readnb(&rio, buf, sizeof(buf));
        if (len < 0) {
            sio_eprintf("Failed to get HTTP response from host\n");
            return false;
        } else if (0 == len) { // EOF
            break;
        }

        // send received HTTP response to client
        if (rio_writen(client_fd, buf, len) < 0) {
            sio_eprintf("Failed to send HTTP response to client\n");
            return false;
        }

        cache_size += len;
    }

    // cache the response if not too large
    if (cache_size <= MAX_OBJECT_SIZE && cache_size > 0) {
        dbg_printf("Caching HTTP response (%s) of size %zu\n", cache_key,
                   cache_size);
        if (!cache_insert(g_cache, cache_key, buf, cache_size)) {
            sio_eprintf("Failed to cache HTTP response\n");
            return false;
        }
    }
    return true;
}

/**
 * Return an HTML file containing error messages to the browser client
 *
 * @param fd Socket descriptor
 * @param errnum HTTP error number
 * @param shortmsg Short error message
 * @param longmsg Long error message
 */
static void clienterror(int fd, const char *errnum, const char *shortmsg,
                        const char *longmsg) {
    dbg_printf("[ERROR] %s: %s (%s)\n", errnum, shortmsg, longmsg);

    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    // build the HTTP response body
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>Proxy</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // overflow
    }

    // build the HTTP response headers
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // overflow
    }

    // write the headers
    if (rio_writen(fd, buf, buflen) < 0) {
        sio_eprintf("Error writing error response headers to client\n");
        return;
    }

    // write the body
    if (rio_writen(fd, body, bodylen) < 0) {
        sio_eprintf("Error writing error response body to client\n");
        return;
    }
}
