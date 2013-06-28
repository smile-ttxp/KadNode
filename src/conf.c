
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "main.h"
#include "log.h"
#include "utils.h"
#include "conf.h"


/* Global object variables */
struct obj_gstate *gstate = NULL;

const char *version = "KadNode v"MAIN_VERSION" ( "
"Features:"
#ifdef CMD
" cmd"
#endif
#ifdef DNS
" dns"
#endif
#ifdef NSS
" nss"
#endif
#ifdef WEB
" web"
#endif
" )\n\n";

const char *usage = "KadNode - A P2P name resolution daemon (IPv4/IPv6)\n"
"A Wrapper for the Kademlia implementation of a Distributed Hash Table (DHT)\n"
"with several interfaces for DNS, an interactive command line, web and NSS.\n"
"\n"
"Usage: kadnode [OPTIONS]*\n"
"\n"
" --node-id	Set the node id. Use --value-id to announce values.\n"
"		Default: <random>\n\n"
" --value-id	Add a '<id>[:<port>]' value to be announced every 30 minutes.\n"
"		This option can occur multiple times.\n\n"
" --user		Change the UUID after start.\n\n"
" --port		Bind to this port.\n"
"		Default: "DHT_PORT"\n\n"
" --mcast-addr	Use multicast address for bootstrapping.\n"
"		Default: "DHT_ADDR4_MCAST" / "DHT_ADDR6_MCAST"\n\n"
" --disable-mcast Disable multicast.\n\n"
" --ifce		Bind to this interface.\n"
"		Default: <any>\n\n"
" --daemon	Run the node in background.\n\n"
" --verbosity	Verbosity level: quiet, verbose or debug.\n"
"		Default: verbose\n\n"
" --pidfile	Write process pid to a file.\n\n"
#ifdef CMD
" --cmd-port	Bind the remote control interface to this local port.\n"
"		Default: "CMD_PORT"\n\n"
#endif
#ifdef DNS
" --dns-port	Bind the DNS server to this local port.\n"
"		Default: "DNS_PORT"\n\n"
#endif
#ifdef NSS
" --nss-port	Bind the Network Service Switch to this local port.\n"
"		Default: "NSS_PORT"\n\n"
#endif
#ifdef WEB
" --web-port	Bind the web server to this local port.\n"
"		Default: "WEB_PORT"\n\n"
#endif
" --mode	Enable IPv4 or IPv6 mode for the DHT.\n"
"		Default: ipv4\n\n"
" -h, --help	Print this help.\n\n"
" -v, --version	Print program version.\n\n";

void conf_init() {
	gstate = (struct obj_gstate *) malloc( sizeof(struct obj_gstate) );

	memset( gstate, '\0', sizeof(struct obj_gstate) );

	id_random( gstate->node_id, SHA_DIGEST_LENGTH );

	gstate->is_running = 1;

#ifdef DEBUG
	gstate->verbosity = VERBOSITY_DEBUG;
#else
	gstate->verbosity = VERBOSITY_VERBOSE;
#endif

	gstate->af = AF_INET;
	gstate->sock = -1;
	gstate->dht_port = strdup( DHT_PORT );
	gstate->mcast_addr = strdup( DHT_ADDR4_MCAST );

#ifdef CMD
	gstate->cmd_port = strdup( CMD_PORT );
#endif

#ifdef DNS
	gstate->dns_port = strdup( DNS_PORT );
#endif

#ifdef NSS
	gstate->nss_port = strdup( NSS_PORT );
#endif

#ifdef WEB
	gstate->web_port = strdup( WEB_PORT );
#endif
}

void conf_check() {
	char hexbuf[HEX_LEN+1];

	log_info( "Starting KadNode v"MAIN_VERSION );
	log_info( "Own ID: %s", str_id( gstate->node_id, hexbuf ) );
	log_info( "Kademlia mode: %s", (gstate->af == AF_INET) ? "IPv4" : "IPv6");

	if( gstate->is_daemon ) {
		log_info( "Mode: Daemon" );
	} else {
		log_info( "Mode: Foreground" );
	}

	switch( gstate->verbosity ) {
		case VERBOSITY_QUIET:
			log_info( "Verbosity: Quiet" );
			break;
		case VERBOSITY_VERBOSE:
			log_info( "Verbosity: Verbose" );
			break;
		case VERBOSITY_DEBUG:
			log_info( "Verbosity: Debug" );
			break;
		default:
			log_err( "Invalid verbosity level." );
	}
}

void conf_free() {

	free( gstate->user );
	free( gstate->pid_file );
	free( gstate->dht_port );
	free( gstate->dht_ifce );
	free( gstate->mcast_addr );

#ifdef CMD
	free( gstate->cmd_port );
#endif
#ifdef DNS
	free( gstate->dns_port );
#endif
#ifdef NSS
	free( gstate->nss_port );
#endif
#ifdef WEB
	free( gstate->web_port );
#endif

	free( gstate );
}

void conf_arg_expected( const char *var ) {
	log_err( "CFG: Argument expected for option %s.", var );
}

void conf_no_arg_expected( const char *var ) {
	log_err( "CFG: No argument expected for option %s.", var );
}

/* free the old string and set the new */
void conf_str( const char *var, char** dst, const char *src ) {
	if( src == NULL ) {
		conf_arg_expected( var );
	}

	free( *dst );
	*dst = strdup( src );
}

void conf_add_value( char *var, char *val ) {
	struct value *v, *c;
	unsigned short port;
	char *delim;

	if( val == NULL ) {
		conf_arg_expected( var );
	}

	/* Split identifier and optional port */
	delim = strchr( val, ':' );
	if( delim ) {
		*delim = '\0';
		port = atoi( delim + 1 );
	} else {
		port = 1;
	}

	if( port < 1 || port > 65535 ) {
		log_err( "CFG: Invalid port used for value: %s", val );
	}

	v = calloc( 1, sizeof(struct value) );

	/* Add new value */
	id_compute( v->value_id, val );
	v->port = port;

	c = gstate->values;
	if( c == NULL ) {
		gstate->values = v;
		return;
	}

	while( c != NULL ) {
		if( id_equal( c->value_id, v->value_id ) ) {
			log_err( "CFG: Duplicate identifier for '%s': %s", var, val );
		}

		if( c->next == NULL ) {
			c->next = v;
			return;
		}

		c = c->next;
	}

	log_crit( "CFG: Found value list loop." );
}

void conf_handle( char *var, char *val ) {

	if( match( var, "--node-id" ) ) {
		/* Compute node id */
		id_compute( gstate->node_id, val );
	} else if( match( var, "--value-id" ) ) {
		conf_add_value( var, val );
	} else if( match( var, "--pidfile" ) ) {
		conf_str( var, &gstate->pid_file, val );
	} else if( match( var, "--verbosity" ) ) {
		if( match( val, "quiet" ) ) {
			gstate->verbosity = VERBOSITY_QUIET;
		} else if( match( val, "verbose" ) ) {
			gstate->verbosity = VERBOSITY_VERBOSE;
		} else if( match( val, "debug" ) ) {
			gstate->verbosity = VERBOSITY_DEBUG;
		} else {
			log_err( "CFG: Invalid verbosity argument." );
		}
#ifdef CMD
	} else if( match( var, "--cmd-port" ) ) {
		conf_str( var, &gstate->cmd_port, val );
#endif
#ifdef DNS
	} else if( match( var, "--dns-port" ) ) {
		conf_str( var, &gstate->dns_port, val );
#endif
#ifdef NSS
	} else if( match( var, "--nss-port" ) ) {
		conf_str( var, &gstate->nss_port, val );
#endif
#ifdef WEB
	} else if( match( var, "--web-port" ) ) {
		conf_str( var, &gstate->web_port, val );
#endif
	} else if( match( var, "--mode" ) ) {
		if( val && match( val, "ipv4" ) ) {
			gstate->af = AF_INET;
			if( strcmp( gstate->mcast_addr, DHT_ADDR4_MCAST ) == 0 ) {
				conf_str( var, &gstate->mcast_addr, DHT_ADDR4_MCAST );
			}
		} else if( val && match( val, "ipv6" ) ) {
			gstate->af = AF_INET6;
			if( strcmp( gstate->mcast_addr, DHT_ADDR4_MCAST ) == 0 ) {
				conf_str( var, &gstate->mcast_addr, DHT_ADDR6_MCAST );
			}
		} else {
			log_err("CFG: Value 'ipv4' or 'ipv6' for parameter --mode expected.");
		}
	} else if( match( var, "--port" ) ) {
		conf_str( var, &gstate->dht_port, val );
	} else if( match( var, "--mcast-addr" ) ) {
		conf_str( var, &gstate->mcast_addr, val );
	} else if( match( var, "--disable-mcast" ) ) {
		if( val != NULL ) {
			conf_no_arg_expected( var );
		} else {
			memset( &gstate->time_mcast, 0xFF, sizeof(time_t) );
		}
	} else if( match( var, "--ifce" ) ) {
		conf_str( var, &gstate->dht_ifce, val );
	} else if( match( var, "--user" ) ) {
		conf_str( var, &gstate->user, val );
	} else if( match( var, "--daemon" ) ) {
		if( val != NULL ) {
			conf_no_arg_expected( var );
		} else {
			gstate->is_daemon = 1;
		}
	} else if( match( var, "-h" ) || match( var, "--help" ) ) {
		printf( "%s", usage );
		exit( 0 );
	} else if( match( var, "-v" ) || match( var, "--version" ) ) {
		printf( "%s", version );
		exit( 0 );
	} else {
		log_err( "CFG: Unknown command line option '%s'", var );
	}
}

void conf_load( int argc, char **argv ) {
	unsigned int i;

	if( argv == NULL ) {
		return;
	}

	for( i = 1; i < argc; i++ ) {
		if( argv[i][0] == '-' ) {
			if( i+1 < argc && argv[i+1][0] != '-' ) {
				/* -x abc */
				conf_handle( argv[i], argv[i+1] );
				i++;
			} else {
				/* -x -y => -x */
				conf_handle( argv[i], NULL );
			}
		} else {
			/* x */
			conf_handle( argv[i], NULL );
		}
	}
}