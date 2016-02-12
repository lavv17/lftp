/*
 * lftp - file transfer program
 *
 * Copyright (c) 2016 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "HttpAuth.h"
#include "HttpHeader.h"

xarray_p<HttpAuth> HttpAuth::cache;

HttpAuth::Challenge::Challenge(const char *p_chal)
{
   const char *end=p_chal+strlen(p_chal);

   // challenge   = auth-scheme 1*SP 1#auth-param
   const char *space=strchr(p_chal,' ');
   if(!space || space==p_chal)
      return;
   scheme.nset(p_chal,space-p_chal).c_ucfirst();

   // auth-param     = token "=" ( token | quoted-string )
   const char *auth_param=space+1;
   while(auth_param<end) {
      const char *eq=strchr(auth_param,'=');
      SetParam(auth_param,eq-auth_param,
	 HttpHeader::extract_quoted_value(eq+1,&space));
      auth_param=(*space==' '?space+1:space);
   }
}

bool HttpAuth::New(target_t t,const char *p_uri,Challenge *p_chal,
   const char *p_user,const char *p_pass)
{
   Ref<Challenge> chal(p_chal);
   const xstring& scheme=chal->GetScheme();
   Ref<HttpAuth> auth;
   if(scheme.eq("Basic")) {
      auth=new HttpAuthBasic(t,p_uri,chal.borrow(),p_user,p_pass);
   } else if(scheme.eq("Digest")) {
//       auth=new HttpAuthDigest(t,p_uri,chal.borrow(),p_user,p_pass);
   }
   if(!auth)
      return false;
   CleanCache(t,p_uri,p_user);
   cache.append(auth.borrow());
   return true;
}

bool HttpAuth::Matches(target_t t,const char *p_uri,const char *p_user)
{
   if(this->target!=t)
      return false;
   if(this->user.ne(p_user))
      return false;
   if(!uri.prefixes(p_uri))
      return false;
   return true;
}

void HttpAuth::CleanCache(target_t t,const char *p_uri,const char *p_user)
{
   for(int i=cache.length()-1; i>=0; i--) {
      if(cache[i]->Matches(t,p_uri,p_user))
	 cache.remove(i);
   }
}

HttpAuth *HttpAuth::Get(target_t t,const char *p_uri,const char *p_user)
{
   for(int i=cache.length()-1; i>=0; i--) {
      if(cache[i]->Matches(t,p_uri,p_user))
	 return cache[i];
   }
   return 0;
}

void HttpAuthBasic::Send(const SMTaskRef<IOBuffer>& buf)
{
   xstring& auth=xstring::get_tmp(user).append(':').append(pass);
   char *buf64=string_alloca(base64_length(auth.length())+1);
   base64_encode(auth,buf64,auth.length());
   buf->Format("%s: Basic %s\r\n",GetHeader(),buf64);
}
