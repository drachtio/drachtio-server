#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <getopt.h>

#define PATHLEN (256)
char cert_file[PATHLEN] ;
char key_file[PATHLEN];
char chain_file[PATHLEN];

void usage() {
    fprintf(stderr, "usage: ssltest --key <keyfile> --cert <certfile> --chain <chainfile>\n") ;
    exit(-1) ;
}
void parse_cmd_args( int argc, char** argv ) {
    int c ;

    memset( cert_file, 0, PATHLEN) ;
    memset( key_file, 0, PATHLEN) ;
    memset( chain_file, 0, PATHLEN) ;

    while (1)
    {
        static struct option long_options[] =
        {
            /* These options set a flag. */
            //{"daemon", no_argument,       &m_bDaemonize, true},
            
            /* These options don't set a flag.
             We distinguish them by their indices. */
            {"cert",    required_argument, 0, 'c'},
            {"key",    required_argument, 0, 'k'},
            {"chain",    required_argument, 0, 'r'},
            //{"version",    no_argument, 0, 'v'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        
        c = getopt_long (argc, argv, "c:k:",
                         long_options, &option_index);
        
        /* Detect the end of the options. */
        if (c == -1)
            break;
        
        switch (c)
        {
            case 0:
                /* If this option set a flag, do nothing else now. */
                if (long_options[option_index].flag != 0)
                    break;
                printf("option %s with arg: %s\n", long_options[option_index].name, optarg) ;
                break;
                                    
            case 'c':
                strncpy(cert_file, optarg, PATHLEN) ; 
                break;

            case 'k':
                strncpy(key_file, optarg, PATHLEN) ; 
                break;
                                                        
            case 'r':
                strncpy(chain_file, optarg, PATHLEN) ; 
                break;
                                                        
            case '?':
                /* getopt_long already printed an error message. */
                break;
                
            default:
                abort ();
        }
    }

    if( 0 == strlen( cert_file ) || 0 == strlen( key_file )|| 0 == strlen( chain_file ) ) {
        usage() ;
    }
            
}

int create_socket(int port)
{
    int s;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
  perror("Unable to create socket");
  exit(EXIT_FAILURE);
    }

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
  perror("Unable to bind");
  exit(EXIT_FAILURE);
    }

    if (listen(s, 1) < 0) {
  perror("Unable to listen");
  exit(EXIT_FAILURE);
    }

    return s;
}

void init_openssl()
{ 
    SSL_load_error_strings(); 
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl()
{
    EVP_cleanup();
}

SSL_CTX *create_context()
{
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();

    ctx = SSL_CTX_new(method);
    if (!ctx) {
  perror("Unable to create SSL context");
  ERR_print_errors_fp(stderr);
  exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_context(SSL_CTX *ctx)
{
    //SSL_CTX_set_ecdh_auto(ctx, 1);
    SSL_CTX_set_tmp_ecdh(ctx, EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));

    /* Set the key and cert */
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) < 0) {
        ERR_print_errors_fp(stderr);
  exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) < 0 ) {
        ERR_print_errors_fp(stderr);
  exit(EXIT_FAILURE);
    }

    if(SSL_CTX_load_verify_locations(ctx, chain_file, NULL) < 0 ) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
                                  
}

int main(int argc, char **argv)
{
    int sock;
    SSL_CTX *ctx;

    parse_cmd_args(argc, argv) ;
    init_openssl();
    ctx = create_context();

    configure_context(ctx);

    sock = create_socket(4433);

    /* Handle connections */
    while(1) {
        struct sockaddr_in addr;
        uint len = sizeof(addr);
        SSL *ssl;
        const char reply[] = "test\n";

        int client = accept(sock, (struct sockaddr*)&addr, &len);
        if (client < 0) {
            perror("Unable to accept");
            exit(EXIT_FAILURE);
        }

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);

        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        }
        else {
            SSL_write(ssl, reply, strlen(reply));
        }

        SSL_free(ssl);
        close(client);
    }

    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();
}
