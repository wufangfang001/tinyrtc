/**
 * @file network.h
 * @brief Network interface and socket abstraction
 */

#ifndef TINYRTC_NETWORK_H
#define TINYRTC_NETWORK_H

#include "tinyrtc/tinyrtc.h"
#include <aosl/aosl.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Socket address structure
 */
typedef struct {
    bool is_ipv6;              /**< IPv6 address if true, IPv4 if false */
    uint16_t port;              /**< Port number */
    union {
        uint8_t ipv4[4];         /**< IPv4 address bytes */
        uint8_t ipv6[16];        /**< IPv6 address bytes */
    } addr;
} tinyrtc_socket_addr_t;

/**
 * @brief Convert string to socket address
 *
 * @param str IP address string
 * @param port Port number
 * @param addr Output address
 * @return true on success
 */
bool tinyrtc_socket_addr_from_string(const char *str, uint16_t port, tinyrtc_socket_addr_t *addr);

/**
 * @brief Convert socket address to string
 *
 * @param addr Address to convert
 * @param buffer Output buffer
 * @param buffer_len Buffer length
 * @return Pointer to buffer
 */
char *tinyrtc_socket_addr_to_string(const tinyrtc_socket_addr_t *addr, char *buffer, size_t buffer_len);

/**
 * @brief Compare two socket addresses for equality
 *
 * @param a First address
 * @param b Second address
 * @return true if addresses are equal
 */
bool tinyrtc_socket_addr_equal(const tinyrtc_socket_addr_t *a, const tinyrtc_socket_addr_t *b);

/**
 * @brief UDP socket handle
 */
typedef struct tinyrtc_udp_socket tinyrtc_udp_socket_t;

/**
 * @brief Create UDP socket
 *
 * @param ctx TinyRTC context
 * @param bind_addr Address to bind (port 0 for auto)
 * @return New socket handle, NULL on error
 */
tinyrtc_udp_socket_t *tinyrtc_udp_socket_create(
    tinyrtc_context_t *ctx,
    tinyrtc_socket_addr_t *bind_addr);

/**
 * @brief Destroy UDP socket
 *
 * @param socket Socket to destroy
 */
void tinyrtc_udp_socket_destroy(tinyrtc_udp_socket_t *socket);

/**
 * @brief Get local address bound to socket
 *
 * @param socket Socket
 * @param out_addr Output address
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_udp_socket_get_local_addr(
    tinyrtc_udp_socket_t *socket,
    tinyrtc_socket_addr_t *out_addr);

/**
 * @brief Send packet to remote address
 *
 * @param socket Socket
 * @param remote Remote address
 * @param data Packet data
 * @param len Packet length
 * @return Number of bytes sent, negative on error
 */
int tinyrtc_udp_socket_send(
    tinyrtc_udp_socket_t *socket,
    const tinyrtc_socket_addr_t *remote,
    const uint8_t *data,
    size_t len);

/**
 * @brief Receive packet from socket
 *
 * @param socket Socket
 * @param buffer Receive buffer
 * @param max_len Maximum buffer length
 * @param out_sender Output sender address
 * @return Number of bytes received, negative on error
 */
int tinyrtc_udp_socket_recv(
    tinyrtc_udp_socket_t *socket,
    uint8_t *buffer,
    size_t max_len,
    tinyrtc_socket_addr_t *out_sender);

/**
 * @brief Set socket non-blocking
 *
 * @param socket Socket
 * @param nonblocking true for non-blocking, false blocking
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_udp_socket_set_nonblocking(
    tinyrtc_udp_socket_t *socket,
    bool nonblocking);

/**
 * @brief Get underlying AOSL socket file descriptor
 *
 * @param socket Socket
 * @return AOSL socket
 */
aosl_socket_t tinyrtc_udp_socket_get_aosl_socket(tinyrtc_udp_socket_t *socket);

/**
 * @brief Network interface enumeration result
 */
typedef struct {
    char name[64];              /**< Interface name */
    tinyrtc_socket_addr_t addr; /**< IP address */
    bool is_up;                 /**< Interface is up */
    bool is_loopback;           /**< Loopback interface */
} tinyrtc_interface_t;

/**
 * @brief Enumerate network interfaces
 *
 * @param ctx TinyRTC context
 * @param interfaces Array to fill
 * @param max_interfaces Maximum number of interfaces
 * @return Number of interfaces found, negative on error
 */
int tinyrtc_enumerate_interfaces(
    tinyrtc_context_t *ctx,
    tinyrtc_interface_t *interfaces,
    int max_interfaces);

/**
 * @brief Resolve hostname to IP address
 *
 * @param ctx TinyRTC context
 * @param hostname Hostname to resolve
 * @param port Port number
 * @param addrs Output array of addresses
 * @param max_addrs Maximum addresses
 * @return Number of addresses resolved
 */
int tinyrtc_resolve_hostname(
    tinyrtc_context_t *ctx,
    const char *hostname,
    uint16_t port,
    tinyrtc_socket_addr_t *addrs,
    int max_addrs);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_NETWORK_H */
