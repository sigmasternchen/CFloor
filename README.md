# CFloor

[![Test Suite](https://github.com/overflowerror/CFloor/actions/workflows/test-suite.yml/badge.svg)](https://github.com/overflowerror/CFloor/actions/workflows/test-suite.yml)

## A minimal event-based webserver written in C

The title already says everything.

## What does "event-based" mean?

The more traditional approach is to fork for every new connection. That yields the advantage of having complete seperation of the connections as well as a much simpler program structure. A big disadvantage is that the server is more susceptible for things like Slow-Lorris attacks. Also the resource consumption is higher.

This webserver handles all connection (for a given bind) in the same thread. All sockets are set as non-blocking and asynchronous meaning that new data on a socket creates a SIGIO signal that causes the data handler to check every active connection for new data. If the HTTP header for a connection is complete the handler for the site is started in a new thread.

The consequence is a very slim memory footprint.

## Feature-Set

- The server can bind to multible addresses at once.
- It can be compiled with full SSL support (OpenSSL) on a per-bind basis.
- Virtual host ("site") support including hostname wildcards
- Basic file handling with optional indexes
- CGI/1.1 support
- Dynamic logging (+ additional access log)
- All settings can be specified via a config file.

Features yet to implement:
- Keep-Alive
- Full SSL-certificate-chain
- Dynamic mods (handlers, ...)

## Modability

I designed the webserver to be as flexible as possible. If the config module is not in use every aspect of the server can be controlled in a fine-grained manner.

New handlers can be added using the `handlerGetter_t` type.

## Config File Format

```
CONFIG           := { CONFIG_ITEM SP }
CONFIG_ITEM      := BIND_CONFIG | LOGGING_CONFIG
BIND_CONFIG      := "bind" SP BIND_ADDR SP "{" SP { BIND_ITEM SP } "}"
BIND_ADDR        := BIND_IP ":" PORT_NO
BIND_IP          := "*" | IP4_ADDR | IP6_ADDR
BIND_ITEM        := SSL_CONFIG | SITE_CONFIG
SSL_CONFIG       := "ssl" SP "{" SP { SSL_ITEM SP } "}"
SSL_ITEM         := SSL_KEY | SSL_CERT
SSL_KEY          := "key" SP "=" SP FILENAME
SSL_CERT         := "cert" SP "=" SP FILENAME
SITE_CONFIG      := "site" SP "{" SP { SITE_ITEM SP } "}"
SITE_ITEM        := SITE_HOSTNAME | SITE_ROOT | HANDLER_CONFIG
SITE_HOSTNAME    := HOSTNAME_KEY SP "=" SP HOSTNAME
HOSTNAME_KEY     := "hostname" | "alias"
SITE_ROOT        := "root" SP "=" SP FILENAME
HANDLER_CONFIG   := "handler" SP FILENAME SP "{" SP { HANDLER_ITEM SP } "}"
HANDLER_ITEM     := HANDLER_TYPE | HANDLER_SETTINGS
HANDLER_TYPE     := "type" SP "=" SP HANDLER_TYPE_H
HANDLER_SETTINGS := HANDLER_INDEX
LOGGING_CONFIG   := "logging" SP "{" SP { LOGGING_ITEM SP } "}"
LOGGING_ITEM     := LOGGING_ACCESS | LOGGING_SERVER | LOGGING_VERBOSE
LOGGING_ACCESS   := "access" SP "=" SP FILENAME
LOGGING_SERVER   := "server" SP "=" SP FILENAME
LOGGING_VERBOSE  := "verbosity" SP "=" SP VERBOSITY

HANDLER_TYPE_H   := "file" | "cgi"
HANDLER_INDEX    := "index" SP "=" SP FILENAME
VERBOSITY        := "debug" | "info" | "warn" | "error"

SP               := SPH [ SPH ]
SPH              := "\n" | "\t" | " "

IP4_ADDR         ... IPv4 address
IP6_ADDR         ... IPv6 address
PORT_NO          ... TCP port number
FILENAME         ... a filename
HOSTNAME         ... fully-qualified domain name
```
