#include "config.h"
#include <FtpListInfo.h>
#include <ftpclass.h>
#include <stdio.h>

const int COUNT=79;
char data[]=
"modify=20160506140233;perm=adfrw;size=25859;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-comments-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=5718;type=file;UNIX.group=10267;UNIX.mode=0644; comment.php\r\n"
"modify=20160506140233;perm=adfrw;size=8724;type=file;UNIX.group=10267;UNIX.mode=0644; menu.php\r\n"
"modify=20160506140233;perm=adfrw;size=11795;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-filesystem-direct.php\r\n"
"modify=20160506140233;perm=adfrw;size=52036;type=file;UNIX.group=10267;UNIX.mode=0644; update-core.php\r\n"
"modify=20160506140233;perm=adfrw;size=19660;type=file;UNIX.group=10267;UNIX.mode=0644; image.php\r\n"
"modify=20160506140233;perm=adfrw;size=15408;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-filesystem-ssh2.php\r\n"
"modify=20160506140233;perm=adfrw;size=5472;type=file;UNIX.group=10267;UNIX.mode=0644; class-ftp-pure.php\r\n"
"modify=20160506140233;perm=adfrw;size=19205;type=file;UNIX.group=10267;UNIX.mode=0644; export.php\r\n"
"modify=20160506140233;perm=adfrw;size=32458;type=file;UNIX.group=10267;UNIX.mode=0644; image-edit.php\r\n"
"modify=20160506140233;perm=adfrw;size=34295;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-screen.php\r\n"
"modify=20160506140233;perm=adfrw;size=38531;type=file;UNIX.group=10267;UNIX.mode=0644; ms.php\r\n"
"modify=20160506140233;perm=adfrw;size=1083;type=file;UNIX.group=10267;UNIX.mode=0644; noop.php\r\n"
"modify=20160506140233;perm=adfrw;size=6304;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-site-icon.php\r\n"
"modify=20160506140233;perm=adfrw;size=123835;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-upgrader.php\r\n"
"modify=20160506140233;perm=adfrw;size=38420;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=11013;type=file;UNIX.group=10267;UNIX.mode=0644; class-walker-nav-menu-edit.php\r\n"
"modify=20160506140233;perm=adfrw;size=5965;type=file;UNIX.group=10267;UNIX.mode=0644; screen.php\r\n"
"modify=20160506140233;perm=adfrw;size=1289;type=file;UNIX.group=10267;UNIX.mode=0644; ms-admin-filters.php\r\n"
"modify=20160506140233;perm=adfrw;size=23183;type=file;UNIX.group=10267;UNIX.mode=0644; network.php\r\n"
"modify=20160506140233;perm=adfrw;size=34967;type=file;UNIX.group=10267;UNIX.mode=0644; deprecated.php\r\n"
"modify=20160506140233;perm=adfrw;size=76330;type=file;UNIX.group=10267;UNIX.mode=0644; template.php\r\n"
"modify=20160506140233;perm=adfrw;size=30022;type=file;UNIX.group=10267;UNIX.mode=0644; plugin-install.php\r\n"
"modify=20160506140233;perm=adfrw;size=4194;type=file;UNIX.group=10267;UNIX.mode=0644; class-walker-category-checklist.php\r\n"
"modify=20160506140233;perm=adfrw;size=17960;type=file;UNIX.group=10267;UNIX.mode=0644; continents-cities.php\r\n"
"modify=20160506140233;perm=adfrw;size=1410;type=file;UNIX.group=10267;UNIX.mode=0644; edit-tag-messages.php\r\n"
"modify=20160506140233;perm=adfrw;size=2872;type=file;UNIX.group=10267;UNIX.mode=0644; admin.php\r\n"
"modify=20160506140233;perm=adfrw;size=16903;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-plugin-install-list-table.php\r\n"
"modify=20160506152856;perm=flcdmpe;type=pdir;UNIX.group=10267;UNIX.mode=0755; ..\r\n"
"modify=20160506140233;perm=adfrw;size=9500;type=file;UNIX.group=10267;UNIX.mode=0644; widgets.php\r\n"
"modify=20160506140233;perm=adfrw;size=52329;type=file;UNIX.group=10267;UNIX.mode=0644; dashboard.php\r\n"
"modify=20160506140233;perm=adfrw;size=7838;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-links-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=39256;type=file;UNIX.group=10267;UNIX.mode=0644; nav-menu.php\r\n"
"modify=20160506140233;perm=adfrw;size=4149;type=file;UNIX.group=10267;UNIX.mode=0644; options.php\r\n"
"modify=20160506140233;perm=adfrw;size=8242;type=file;UNIX.group=10267;UNIX.mode=0644; translation-install.php\r\n"
"modify=20160506140233;perm=adfrw;size=26999;type=file;UNIX.group=10267;UNIX.mode=0644; class-ftp.php\r\n"
"modify=20160506140233;perm=adfrw;size=9095;type=file;UNIX.group=10267;UNIX.mode=0644; bookmark.php\r\n"
"modify=20160506140233;perm=adfrw;size=1970;type=file;UNIX.group=10267;UNIX.mode=0644; credits.php\r\n"
"modify=20160506140233;perm=adfrw;size=29492;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-upgrader-skins.php\r\n"
"modify=20160506140233;perm=adfrw;size=14030;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-filesystem-ftpext.php\r\n"
"modify=20160506140233;perm=adfrw;size=49792;type=file;UNIX.group=10267;UNIX.mode=0644; meta-boxes.php\r\n"
"modify=20160506140233;perm=adfrw;size=15860;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-ms-sites-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=50550;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-posts-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=195702;type=file;UNIX.group=10267;UNIX.mode=0644; class-pclzip.php\r\n"
"modify=20160506140233;perm=adfrw;size=37012;type=file;UNIX.group=10267;UNIX.mode=0644; schema.php\r\n"
"modify=20160506140233;perm=adfrw;size=17906;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-terms-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=31305;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-plugins-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=102273;type=file;UNIX.group=10267;UNIX.mode=0644; media.php\r\n"
"modify=20160506140233;perm=adfrw;size=4926;type=file;UNIX.group=10267;UNIX.mode=0644; class-walker-nav-menu-checklist.php\r\n"
"modify=20160506140233;perm=adfrw;size=22969;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-filesystem-base.php\r\n"
"modify=20160506140233;perm=adfrw;size=14660;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-theme-install-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=12593;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-ms-users-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=19673;type=file;UNIX.group=10267;UNIX.mode=0644; update.php\r\n"
"modify=20160506140233;perm=adfrw;size=4319;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-internal-pointers.php\r\n"
"modify=20160506140233;perm=adfrw;size=3612;type=file;UNIX.group=10267;UNIX.mode=0644; list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=7698;type=file;UNIX.group=10267;UNIX.mode=0644; taxonomy.php\r\n"
"modify=20160506140233;perm=adfrw;size=6290;type=file;UNIX.group=10267;UNIX.mode=0644; theme-install.php\r\n"
"modify=20160506140233;perm=adfrw;size=2862;type=file;UNIX.group=10267;UNIX.mode=0644; ms-deprecated.php\r\n"
"modify=20160506153131;perm=flcdmpe;type=cdir;UNIX.group=10267;UNIX.mode=0755; .\r\n"
"modify=20160506140233;perm=adfrw;size=6331;type=file;UNIX.group=10267;UNIX.mode=0644; import.php\r\n"
"modify=20160506140233;perm=adfrw;size=58630;type=file;UNIX.group=10267;UNIX.mode=0644; post.php\r\n"
"modify=20160506140233;perm=adfrw;size=4661;type=file;UNIX.group=10267;UNIX.mode=0644; admin-filters.php\r\n"
"modify=20160506140233;perm=adfrw;size=51492;type=file;UNIX.group=10267;UNIX.mode=0644; file.php\r\n"
"modify=20160506140233;perm=adfrw;size=19841;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-ms-themes-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=11107;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-filesystem-ftpsockets.php\r\n"
"modify=20160506140233;perm=adfrw;size=17000;type=file;UNIX.group=10267;UNIX.mode=0644; user.php\r\n"
"modify=20160506140233;perm=adfrw;size=8518;type=file;UNIX.group=10267;UNIX.mode=0644; class-ftp-sockets.php\r\n"
"modify=20160506140233;perm=adfrw;size=9376;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-themes-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=26293;type=file;UNIX.group=10267;UNIX.mode=0644; misc.php\r\n"
"modify=20160506140233;perm=adfrw;size=67625;type=file;UNIX.group=10267;UNIX.mode=0644; plugin.php\r\n"
"modify=20160506140233;perm=adfrw;size=14941;type=file;UNIX.group=10267;UNIX.mode=0644; revision.php\r\n"
"modify=20160506140233;perm=adfrw;size=26681;type=file;UNIX.group=10267;UNIX.mode=0644; theme.php\r\n"
"modify=20160506140233;perm=adfrw;size=92655;type=file;UNIX.group=10267;UNIX.mode=0644; ajax-actions.php\r\n"
"modify=20160506140233;perm=adfrw;size=7224;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-importer.php\r\n"
"modify=20160506140233;perm=adfrw;size=1472;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-post-comments-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=49695;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-press-this.php\r\n"
"modify=20160506140233;perm=adfrw;size=22416;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-media-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=15813;type=file;UNIX.group=10267;UNIX.mode=0644; class-wp-users-list-table.php\r\n"
"modify=20160506140233;perm=adfrw;size=88433;type=file;UNIX.group=10267;UNIX.mode=0644; upgrade.php\r\n"
;

int main()
{
   FA *ftp=FileAccess::New("ftp");
   if(!ftp) {
      fprintf(stderr,"ftp=NULL\n");
      return 1;
   }

   int err=0;
   FileSet *set=ftp->ParseLongList(data,sizeof(data),&err);
   if(!set) {
      fprintf(stderr,"set=NULL\n");
      return 1;
   }
   if(err>0) {
      fprintf(stderr,"err=%d\n",err);
      return 1;
   }
   if(set->count()!=COUNT) {
      fprintf(stderr,"count=%d (expected %d)\n",set->count(),COUNT);
      return 1;
   }
   return 0;
}
