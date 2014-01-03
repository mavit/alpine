#!/bin/sh
#
# $Id: mailtrfc.sh 761 2007-10-23 22:35:18Z hubert@u.washington.edu $
#
#            T H E    P I N E    M A I L   S Y S T E M
#
#   Laurence Lundblade and Mike Seibel
#   Networks and Distributed Computing
#   Computing and Communications
#   University of Washington
#   Administration Building, AG-44
#   Seattle, Washington, 98195, USA
#   Internet: lgl@CAC.Washington.EDU
#             mikes@CAC.Washington.EDU
#
#   Please address all bugs and comments to "pine-bugs@cac.washington.edu"
#
#
#   Pine and Pico are registered trademarks of the University of Washington.
#   No commercial use of these trademarks may be made without prior written
#   permission of the University of Washington.
#
#   Pine, Pico, and Pilot software and its included text are Copyright
#   1989-1996 by the University of Washington.
#
#   The full text of our legal notices is contained in the file called
#   CPYRIGHT, included with this distribution.
#
#
#   Pine is in part based on The Elm Mail System:
#    ***********************************************************************
#    *  The Elm Mail System  -  Revision: 2.13                             *
#    *                                                                     *
#    * 			Copyright (c) 1986, 1987 Dave Taylor               *
#    * 			Copyright (c) 1988, 1989 USENET Community Trust    *
#    ***********************************************************************
# 
#


# 
# mailtrfc.sh -- A shell script to analyze the mail traffic as logged in
# /usr/spool/mqueue/syslog*. This currently as the University of Washington
# domains wired in and needs to be made more general. Also, lots more
# formats of message ID's could be added.
#



org=`awk '/^domain/ {print $2}' < /etc/resolv.conf`
domain=`echo $org | sed -e 's/^[^.]*\.//'`
host=`hostname`".$org"

echo "Domain: $domain"
echo "Organization: $org"
echo "Hostname: $host"

sed -n -e '/message-id/s/^.*</</p' |
awk 'BEGIN {mailers[0] =  "Other";
            mailers[1] =  "Pine";
            mailers[2] =  "MailManager";
            mailers[3] =  "sendmail";
            mailers[4] =  "BITNET";
            mailers[5] =  "? news ?";
            mailers[6] =  "Sprint";
            mailers[7] =  "X.400";
            mailers[8] =  "Mac MS";
            mailers[9] =  "MMDF";
            mailers[10] = "Andrew";
            mailers[11] = "Columbia MM";
            mailers[12] = "Unknown #1";
            mailers[13] = "EasyMail";
            mailers[14] = "CompuServe";
            mailers[15] = "smail";
            mailers[16] = "MCI Mail";
            mailers[17] = "VAX MAIL(?)";
            mailers[18] = "Gator Mail (?)";
            mailers[19] =  "TOTAL";
            max = 19;}
                                      {mailer = 0;}
     /^<Pine/                         {mailer = 1;}
     /^<MailManager/                  {mailer = 2;}
     /^<[12]?[90]?9[0-9]1?[0-9][1-3]?[0-9]+\.[AaBb][AaBb][0-9]+@/ {mailer = 3;}
     /^<[0-9A-Z]+@/                   {mailer = 4;}
     /^<199[0-9][A-Za-z]..[0-9]*\./   {mailer = 5;}
     /@sprint.com/                    {mailer = 6;}
     /\/[A-Z]*=.*\/[A-Z]*=.*/         {mailer = 7;}
     /^<MacMS\.[0-9]+\.[0-9]+\.[a-z]+@/ {mailer = 8;}
     /^<MAILQUEUE-[0-9]+\.[0-9]+/           {mailer = 9;}
     /^<.[d-l][A-Z0-9a-z=_]+00[A-Za-z0-9_=]+@/ {mailer = 10;}
     /^<CMM\.[0-9]+\.[0-9]+\.[0-9]+/    {mailer = 11 ;}
     /^<9[0-9][JFMASOND][aepuco][nbrylgptvc][0-9][0-9]?\.[0-9]+[a-z]+\./ {mailer = 12;}
     /^<EasyMail\.[0-9]+/               {mailer = 13;}
     /@CompuServe.COM/                  {mailer = 14;}
     /^<m[A-Za-z0-9].....-[0-9A-Za-z].....C@/       {mailer = 15;}
     /@mcimail.com/                     {mailer = 16;}
     /^<9[0-9][01][0-9][0-3][0-9][0-2][0-9][0-5][0-9][0-5][0-9].[0-9a-z]*@/ {mailer = 17;}
     /^<0[0-9][0-9]+\.[0-9][0-9][0-9][0-9]+\.[0-9][0-9]+@/ {mailer=18;}

    

     '"/$domain>/"'              {campus[mailer]++; campus[max]++}
     '"/$org>/"'                 {u[mailer]++; u[max]++}
     '"/$host>/"'                {milton[mailer]++; milton[max]++}
                                 {total[mailer]++; total[max]++}
                                 {if(mailer == 0) printf("-->%s\n",$0)}
     END {
            for(m = 0; m <= max; m++)  {
                printf("%-10.10s", mailers[m]);
                printf(" %11d %11d %11d %11d %11d (%3d%%)\n",  milton[m], u[m] - milton[m], campus[m] -u[m], total[m] - campus[m], total[m], (total[m]*100)/total[max]);
            }
            printf(" ----           (%3d%%)      (%3d%%)      (%3d%%)      (%3d%%)\n", (milton[max]*100)/total[max], ((u[max] - milton[max])*100)/total[max], ((campus[max] - u[max])*100)/total[max], ((total[max] - campus[max])*100)/total[max], (u[max]*100)/total[max]);

        }' > /tmp/syslogx.$$


echo $host $org $domain | \
  awk '{printf("     %.17s %.11s %.11s  Off Campus        Total\n", $1, $2, $3)}'
egrep -v 'TOTAL|----|^-->' /tmp/syslogx.$$ | sort +0.60rn 
egrep  'TOTAL|----' /tmp/syslogx.$$
grep  '^-->' /tmp/syslogx.$$ | sed -e 's/-->//' > other-traffic
rm -f /tmp/syslogx.$$


