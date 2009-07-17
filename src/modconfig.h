#ifdef WITH_MODULES
# define MODULE_NETWORK	   1
# define MODULE_PROTO_FTP  1
# define MODULE_PROTO_HTTP 1
# define MODULE_PROTO_FILE 1
# define MODULE_PROTO_FISH 1
# define MODULE_PROTO_SFTP 1
# define MODULE_CMD_MIRROR 1
# define MODULE_CMD_SLEEP  1
# define MODULE_CMD_TORRENT 1
#endif
/* declarations for use in modules */
CDECL_BEGIN
extern const char *module_depend[];
extern void module_init();
CDECL_END
