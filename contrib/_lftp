#compdef lftp

__site() {
  local bookmarks=$(lftp -c 'bookmark list')
  _alternative 'hosts: :_hosts' 'urls: :_urls' "bookmarks:bookmark:((${bookmarks//$'\t'/:}))"
}

_arguments -S -s \
 "-f[execute commands from the file and exit]: :_files" \
 "-c[execute the commands and exit]:command" \
 "--norc[don't execute rc files from the home directory]" \
 "(- : *)--help[print this help and exit]" \
 "--version[print lftp version and exit]" \
 "-e[execute the command just after selecting]" \
 "-u[use the user/password for authentication]: :_users" \
 "-p[use the port for connection]:port" \
 "-s[assign the connection to this slot]:slot" \
 "-d[switch on debugging mode]" \
 "1:host name, URL or bookmark name:__site"
