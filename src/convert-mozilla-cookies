#!/usr/bin/perl
# Copyright (c) 2001,2005,2007,2010 Alexander V. Lukyanov <lav@yars.free.net>
# See COPYING file (GNU GPL) for complete license.

# This script converts mozilla-style cookies to lftp set commands.

use strict;
use DBI;

my $file=$ARGV[0] if defined $ARGV[0];
$file=qx{
	ls -t \$HOME/.netscape/cookies \\
	      `find \$HOME/.mozilla -name cookies.txt -or -name cookies.sqlite` | head -1
},chomp $file if !defined $file;

print "# converted from $file\n";
my %cookie;
sub add_cookie($$$$$) {
   my ($domain,$path,$secure_bool,$name,$value)=@_;
   for($domain,$path,$name,$value) { s/"/\\"/g; s/'/\\'/g; s/ /%20/g; }
   my $secure='';
   $secure=';secure' if $secure_bool eq 'TRUE' || $secure_bool eq '1';
   $domain="*$domain" if $domain =~ /^\./;
   $path='' if $path eq '/';
   $path=";path=$path" if $path ne '';
   $value="=$value" if $name ne '';
   $cookie{"$domain$path$secure"}.=" $name$value";
}
if($file=~/\.sqlite$/) {
   my $file1="$file.$$.tmp";
   $SIG{__DIE__}=sub { unlink $file1 };
   system('cp',$file,$file1);
   my $dbh=DBI->connect("dbi:SQLite:dbname=$file1",'','');
   unlink $file1;
   my $q=$dbh->prepare("select host,path,isSecure,name,value from moz_cookies");
   $q->execute;
   $q->bind_columns(\my($domain,$path,$secure_bool,$name,$value));
   while($q->fetch) {
      add_cookie($domain,$path,$secure_bool,$name,$value);
   }
} else {
   open COOKIES,'<',$file or die "open($file): $!";
   while(<COOKIES>)
   {
      chomp;
      next if /^#/ or /^$/;
      my ($domain,undef,$path,$secure_bool,$expires,$name,$value)=split /\t/;
      add_cookie($domain,$path,$secure_bool,$name,$value);
   }
}
foreach(sort keys %cookie)
{
   $cookie{$_}=~s/^ //;
   my $c=($_=~/;/?qq{"$_"}:$_);
   print qq{set http:cookie/$c "$cookie{$_}"\n};
}
