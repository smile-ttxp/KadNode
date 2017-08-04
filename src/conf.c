
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>

#include "main.h"
#include "log.h"
#include "utils.h"
#include "conf.h"
#include "peerfile.h"
#include "kad.h"
#ifdef TLS
#include "ext-tls-client.h"
#include "ext-tls-server.h"
#endif
#ifdef BOB
#include "ext-bob.h"
#endif
#ifdef FWD
#include "ext-fwd.h"
#endif
#ifdef __CYGWIN__
#include "windows.h"
#endif

// Global object variables
struct gconf_t *gconf = NULL;

// Program arguments (extendable
static int g_argc = 0;
static char **g_argv = NULL;


const char *kadnode_version_str = "KadNode v"MAIN_VERSION" ("
#ifdef LPD
" lpd"
#endif
#ifdef BOB
" bob"
#endif
#ifdef CMD
" cmd"
#endif
#ifdef NSS
" nss"
#endif
#ifdef DEBUG
" debug"
#endif
#ifdef DNS
" dns"
#endif
#ifdef FWD_NATPMP
" natpmp"
#endif
#ifdef FWD_UPNP
" upnp"
#endif
#ifdef TLS
" tls"
#endif
" )";

const char *kadnode_usage_str = "KadNode - A P2P name resolution daemon.\n"
"A Wrapper for the Kademlia implementation of a Distributed Hash Table (DHT)\n"
"with several optional interfaces (use --version).\n"
"\n"
"Usage: kadnode [OPTIONS]*\n"
"\n"
" --peerfile <file>		Import/Export peers from and to a file.\n\n"
" --peer <addr>			Add a static peer address.\n"
"				This option may occur multiple times.\n\n"
" --user <user>			Change the UUID after start.\n\n"
" --port	<port>			Bind DHT to this port.\n"
"				Default: "DHT_PORT"\n\n"
" --config <file>		Provide a configuration file with one command line\n"
"				option on each line. Comments start after '#'.\n\n"
" --ifname <interface>		Bind to this interface.\n"
"				Default: <any>\n\n"
" --daemon			Run the node in background.\n\n"
" --verbosity <level>		Verbosity level: quiet, verbose or debug.\n"
"				Default: verbose\n\n"
" --pidfile <file>		Write process pid to a file.\n\n"
" --ipv4, -4, --ipv6, -6		Enable IPv4 or IPv6 only mode.\n"
"				Default: IPv4+IPv6\n\n"
" --query-tld <domain>		Top level domain to be handled by KadNode.\n"
"				Default: "QUERY_TLD_DEFAULT"\n\n"
#ifdef LPD
" --lpd-addr <addr>		Set multicast address for Local Peer Discovery.\n"
"				Default: "LPD_ADDR4" / "LPD_ADDR6"\n\n"
" --lpd-disable			Disable multicast to discover local peers.\n\n"
#endif
#ifdef BOB
" --bob-create-key <file>	Generate a new secp256r1 secret key in pem format and\n"
"				write it to the file. The public key will be printed to the terminal.\n\n"
" --bob-load-key <file>		Load a secret key from a file in pem format. The derived public\n"
"				key will be printed to the terminal and announed in the network.\n"
"				The secret key will be used to prove its ownership.\n\n"
#endif
#ifdef CMD
" --cmd-disable-stdin		Disable the local control interface.\n\n"
" --cmd-port <port>		Bind the remote control interface to this local port.\n"
"				Default: "CMD_PORT"\n\n"
#endif
#ifdef DNS
" --dns-port <port>		Bind the DNS server interface to this local port.\n"
"				Default: "DNS_PORT"\n\n"
" --dns-proxy-enable		Enable DNS proxy mode. Reads /etc/resolv.conf by default.\n\n"
" --dns-proxy-server <ip-addr>	IP address of an external DNS server.\n\n"
#endif
#ifdef NSS
" --nss-port <port>		Bind the Network Service Switch to this local port.\n"
"				Default: "NSS_PORT"\n\n"
#endif
#ifdef FWD
" --fwd-disable			Disable UPnP/NAT-PMP to forward router ports.\n\n"
#endif
#ifdef TLS
" --tls-client-cert <path>	Path to file or folder of CA certificates.\n"
"				This option may occur multiple times.\n\n"
" --tls-server-cert <triple>	Add a comma separated triple of server domain, certificate and key.\n"
"				This option may occur multiple times.\n"
"				Example: kanode.p2p,kadnode.crt,kadnode.key\n\n"
#endif
#ifdef __CYGWIN__
" --service-start		Start, install and remove KadNode as Windows service.\n"
" --service-install		KadNode will be started/shut down along with Windows\n"
" --service-remove		or on request by using the Service Control Manager.\n\n"
#endif
" -h, --help			Print this help.\n\n"
" -v, --version			Print program version.\n\n";


void conf_init( void ) {
	gconf = (struct gconf_t *) calloc( 1, sizeof(struct gconf_t) );

	gconf->is_running = 1;

#ifdef DEBUG
	gconf->verbosity = VERBOSITY_DEBUG;
#else
	gconf->verbosity = VERBOSITY_VERBOSE;
#endif
}

// Set default if setting was not set and validate settings
void conf_check( void ) {

	if( gconf->af == 0 ) {
		gconf->af = AF_UNSPEC;
	}

	if( gconf->query_tld == NULL ) {
		gconf->query_tld = strdup( QUERY_TLD_DEFAULT );
	}

	if( gconf->dht_port == NULL ) {
		gconf->dht_port = strdup( DHT_PORT );
	}

#ifdef CMD
	if( gconf->cmd_port == NULL )  {
		gconf->cmd_port = strdup( CMD_PORT );
	}
#endif

#ifdef DNS
	if( gconf->dns_port == NULL ) {
		gconf->dns_port = strdup( DNS_PORT );
	}
#endif

#ifdef NSS
	if( gconf->nss_port == NULL ) {
		gconf->nss_port = strdup( NSS_PORT );
	}
#endif

	if( port_parse( gconf->dht_port, -1 ) < 1 ) {
		log_err( "Invalid DHT port: %s", gconf->dht_port );
		exit( 1 );
	}

#ifdef CMD
	if( port_parse( gconf->cmd_port, -1 ) < 0 ) {
		log_err( "Invalid CMD port: %s", gconf->cmd_port );
		exit( 1 );
	}
#endif

#ifdef DNS
	if( port_parse( gconf->dns_port, -1 ) < 0 ) {
		log_err( "Invalid DNS port: %s", gconf->dns_port );
		exit( 1 );
	}
#endif

#ifdef NSS
	if( port_parse( gconf->nss_port, -1 ) < 0 ) {
		log_err( "Invalid NSS port: %s", gconf->nss_port );
		exit( 1 );
	}
#endif

#ifdef LPD
	IP lpd_addr;

	if( gconf->lpd_addr == NULL ) {
		// Set default multicast address string
		gconf->lpd_addr = strdup(
			(gconf->af == AF_INET) ? LPD_ADDR4 : LPD_ADDR6
		);
	}

	// Parse multicast address string
	if( addr_parse( &lpd_addr, gconf->lpd_addr, LPD_PORT, gconf->af ) != 0 ) {
		log_err( "Failed to parse IP address for: %s", gconf->lpd_addr );
		exit( 1 );
	}

	// Verifiy multicast address
	if( !addr_is_multicast(&lpd_addr) ) {
		log_err( "Multicast address expected: %s", str_addr( &lpd_addr ) );
		exit( 1 );
	}
#endif

	// Store startup time
	gconf->time_now = time( NULL );
	gconf->startup_time = time_now_sec();
}

const char *verbosity_str( int verbosity ) {
	switch( verbosity ) {
		case VERBOSITY_QUIET: return "quiet";
		case VERBOSITY_VERBOSE: return "verbose";
		case VERBOSITY_DEBUG: return "debug";
		default:
			log_err( "Invalid verbosity: %d", verbosity );
			exit( 1 );
	}
}

void conf_info( void ) {
	log_info( "Starting %s", kadnode_version_str );
	log_info( "Net Mode: %s", str_af( gconf->af ) );
	log_info( "Run Mode: %s", gconf->is_daemon ? "daemon" : "foreground" );

	if( gconf->configfile ) {
		log_info( "Configuration File: %s", gconf->configfile );
	}

	log_info( "Verbosity: %s", verbosity_str( gconf->verbosity ) );
	log_info( "Query TLD: %s", gconf->query_tld );
	log_info( "Peer File: %s", gconf->peerfile ? gconf->peerfile : "None" );
#ifdef LPD
	log_info( "LPD Address: %s", (gconf->lpd_disable == 0) ? gconf->lpd_addr : "Disabled" );
#endif
#ifdef DNS
	if( gconf->dns_proxy_enable ) {
		if( gconf->dns_proxy_server ) {
			log_info( "DNS proxy enabled: %s", gconf->dns_proxy_server );
		} else {
			log_info( "DNS proxy enabled: /etc/resolv.conf" );
		}
	}
#endif
}

void conf_free( void ) {
	free( gconf->query_tld );
	free( gconf->user );
	free( gconf->pidfile );
	free( gconf->peerfile );
	free( gconf->dht_port );
	free( gconf->dht_ifname );
	free( gconf->configfile );

#ifdef LPD
	free( gconf->lpd_addr );
#endif
#ifdef CMD
	free( gconf->cmd_port );
#endif
#ifdef DNS
	free( gconf->dns_port );
	free( gconf->dns_proxy_server );
#endif
#ifdef NSS
	free( gconf->nss_port );
#endif

	free( gconf );
}

// Enumerate all options to keep binary size smaller
enum OPCODE {
	oQueryTld,
	oPidFile,
	oPeerFile,
	oPeer,
	oVerbosity,
	oCmdDisableStdin,
	oCmdPort,
	oDnsPort,
	oDnsProxyEnable,
	oDnsProxyServer,
	oNssPort,
	oTlsClientEntry,
	oTlsServerEntry,
	oConfig,
	oIpv4,
	oIpv6,
	oPort,
	oLpdAddr,
	oLpdDisable,
	oFwdDisable,
	oServiceInstall,
	oServiceRemove,
	oServiceStart,
	oBobCreateKey,
	oBobLoadKey,
	oIfname,
	oUser,
	oDaemon,
	oHelp,
	oVersion,
	oUnknown
};

struct option_t {
	const char *name;
	uint16_t num_args;
	uint16_t code;
};

static struct option_t options[] = {
	{"", 0, oUnknown},
	{"--query-tld", 1, oQueryTld},
	{"--pidfile", 1, oPidFile},
	{"--peerfile", 1, oPeerFile},
	{"--peer", 1, oPeer},
	{"--verbosity", 1, oVerbosity},
#ifdef CMD
	{"--cmd-disable-stdin", 0, oCmdDisableStdin},
	{"--cmd-port", 1, oCmdPort},
#endif
#ifdef DNS
	{"--dns-port", 1, oDnsPort},
	{"--dns-proxy-enable", 0, oDnsProxyEnable},
	{"--dns-proxy-server", 1, oDnsProxyServer},
#endif
#ifdef NSS
	{"--nss-port", 1, oNssPort},
#endif
#ifdef TLS
	{"--tls-client-cert", 1, oTlsClientEntry},
	{"--tls-server-cert", 1, oTlsServerEntry},
#endif
	{"--config", 1, oConfig},
	{"--ipv4", 0, oIpv4},
	{"-4", 0, oIpv4},
	{"--ipv6", 0, oIpv6},
	{"-6", 0, oIpv6},
	{"--port", 1, oPort},
#ifdef LPD
	{"--lpd-addr", 1, oLpdAddr},
	{"--lpd-disable", 0, oLpdDisable},
#endif
#ifdef FWD
	{"--fwd-disable", 0, oFwdDisable},
#endif
#ifdef __CYGWIN__
	{"--service-install", 0, oServiceInstall},
	{"--service-remove", 0, oServiceRemove},
	{"--service-start", 0, oServiceStart},
#endif
#ifdef BOB
	{"--bob-create-key", 1, oBobCreateKey},
	{"--bob-load-key", 1, oBobLoadKey},
#endif
	{"--ifname", 1, oIfname},
	{"--user", 1, oUser},
	{"--daemon", 1, oDaemon},
	{"-h", 0, oHelp},
	{"--help", 0, oHelp},
	{"-v", 0, oVersion},
	{"--version", 0, oVersion},
};

const struct option_t *find_option(const char name[]) {
	int i;

	for( i = 0; i < N_ELEMS(options); i++) {
		if( strcmp( name, options[i].name ) == 0 ) {
			return &options[i];
		}
	}

	return &options[0];
}

// Set a string once - error when already set
void conf_str( const char opt[], char *dst[], const char src[] ) {
	if( *dst != NULL ) {
		log_err( "Option was already set: %s", opt );
		exit( 1 );
	}

	*dst = strdup( src );
}

void conf_handle_option( const char opt[], const char val[] ) {
	const struct option_t *option;

	option = find_option( opt );

	if( option->num_args == 1 && val == NULL ) {
		log_err( "Argument expected for option: %s", opt );
		exit( 1 );
	}

	if( option->num_args == 0 && val != NULL ) {
		log_err( "No argument expected for option: %s", opt );
		exit( 1 );
	}

	switch( option->code ) {
		case oQueryTld:
			conf_str( opt, &gconf->query_tld, val );
			break;
		case oPidFile:
			conf_str( opt, &gconf->pidfile, val );
			break;
		case oPeerFile:
			conf_str( opt, &gconf->peerfile, val );
			break;
		case oPeer:
			peerfile_add_peer( val );
			break;
		case oVerbosity:
			if( strcmp( val, "quiet" ) == 0 ) {
				gconf->verbosity = VERBOSITY_QUIET;
			} else if( strcmp( val, "verbose" ) == 0 ) {
				gconf->verbosity = VERBOSITY_VERBOSE;
			} else if( strcmp( val, "debug" ) == 0 ) {
				gconf->verbosity = VERBOSITY_DEBUG;
			} else {
				log_err( "Invalid argument for %s", opt );
				exit( 1 );
			}
			break;
#ifdef CMD
		case oCmdDisableStdin:
			gconf->cmd_disable_stdin = 1;
			break;
		case oCmdPort:
			conf_str( opt, &gconf->cmd_port, val );
			break;
#endif
#ifdef DNS
		case oDnsPort:
			conf_str( opt, &gconf->dns_port, val );
			break;
		case oDnsProxyEnable:
			gconf->dns_proxy_enable = 1;
			break;
		case oDnsProxyServer:
			conf_str( opt, &gconf->dns_proxy_server, val );
			break;
#endif
#ifdef NSS
		case oNssPort:
			conf_str( opt, &gconf->nss_port, val );
			break;
#endif
#ifdef TLS
		case oTlsClientEntry:
			// Add Certificate Authority (CA) entries for the TLS client
			tls_client_add_ca( val );
			break;
		case oTlsServerEntry:
		{
			// Add SNI entries for the TLS server (e.g. foo.p2p,my.cert,my.key)
			char name[128];
			char crt_file[128];
			char key_file[128];

			if( sscanf( val, "%127[^,],%127[^,],%127[^,]", name, crt_file, key_file ) == 3 ) {
				tls_server_add_sni( name, crt_file, key_file );
			} else {
				log_err( "Invalid value format: %s", val );
				exit(1);
			}
			break;
		}
#endif
		case oConfig:
			conf_str( opt, &gconf->configfile, val );
			break;
		case oIpv4:
		case oIpv6:
			if( gconf->af != 0 ) {
				log_err( "IPv4 or IPv6 mode already set: %s", opt );
				exit( 1 );
			}

			gconf->af = (option->code == oIpv6) ? AF_INET6 : AF_INET;
			break;
		case oPort:
			conf_str( opt, &gconf->dht_port, val );
			break;
#ifdef LPD
		case oLpdAddr:
			conf_str( opt, &gconf->lpd_addr, val );
			break;
		case oLpdDisable:
			gconf->lpd_disable = 1;
			break;
#endif
#ifdef FWD
		case oFwdDisable:
			gconf->fwd_disable = 1;
			break;
#endif
#ifdef __CYGWIN__
		case oServiceInstall:
			windows_service_install();
			exit(0);
			break;
		case oServiceRemove:
			windows_service_remove();
			exit(0);
			break;
		case oServiceStart:
			gconf->service_start = 1;
			break;
#endif
		case oIfname:
			conf_str( opt, &gconf->dht_ifname, val );
			break;
		case oUser:
			conf_str( opt, &gconf->user, val );
			break;
		case oDaemon:
			gconf->is_daemon = 1;
			break;
		case oHelp:
			printf( "%s\n", kadnode_usage_str );
			exit( 0 );
			break;
		case oVersion:
			printf( "%s\n", kadnode_version_str );
			exit( 0 );
			break;
#ifdef BOB
		case oBobCreateKey:
			exit( bob_create_key( val ) < 0 );
			break;
		case oBobLoadKey:
			bob_load_key( val );
			break;
#endif
		default:
			log_err( "Unkown parameter: %s", opt );
			exit(1);
	}
}

// Append arguments to g_argv / g_argc
void conf_append( const char opt[], const char val[] ) {
	g_argv = (char**) realloc( g_argv, (g_argc + 3) * sizeof(char*) );
	g_argv[g_argc] = strdup( opt );
	g_argv[g_argc + 1] = val ? strdup( val ) : NULL;
	g_argv[g_argc + 2] = NULL;
	g_argc += 2;
}

void conf_load_file( const char path[] ) {
	char line[256];
	char option[32];
	char value[128];
	char dummy[4];
	char *last;
	struct stat s;
	int ret;
	FILE *file;
	size_t nline;

	if( stat( path, &s ) == 0 && !(s.st_mode & S_IFREG) ) {
		log_err( "File expected: %s", path );
		exit( 1 );
	}

	nline = 0;
	file = fopen( path, "r" );
	if( file == NULL ) {
		log_err( "Cannot open file: %s (%s)", path, strerror( errno ) );
		exit( 1 );
	}

	while( fgets( line, sizeof(line), file ) != NULL ) {
		nline++;

		// Cut off comments
		last = strchr( line, '#' );
		if( last ) {
			*last = '\0';
		}

		ret = sscanf( line, " %31s %127s %3s", option, value, dummy );

		if( ret == 1 || ret == 2) {
			// Prevent recursive inclusion
			if( strcmp( option, "--config " ) == 0) {
				fclose( file );
				log_err( "Option '--config' not allowed inside a configuration file, line %ld.", nline );
				exit( 1 );
			}

			// --option value / --option
			conf_append( option, (ret == 2) ? value : NULL );
		} else if( line[0] != '\0' ) {
			fclose( file );
			log_err( "Invalid line in config file: %s (%d)", path, nline );
			exit( 1 );
		}
	}

	fclose( file );
}


void conf_load_args( int argc, char **argv ) {
	int i;

	// Duplicate memory to get an array that can be appended to
	g_argv = (char**) memdup(argv, (argc + 1) * sizeof(char*));
	g_argc = argc;

	for( i = 1; i < g_argc; i++ ) {
		const char *opt = g_argv[i];
		const char *val = g_argv[i + 1];
		if( val && val[0] != '-') {
			// -x abc
			conf_handle_option( opt, val );
			i++;
		} else {
			// -x
			conf_handle_option( opt, NULL );
		}
	}

	if( gconf->configfile ) {
		conf_load_file( gconf->configfile );
	}

	conf_check();
}
