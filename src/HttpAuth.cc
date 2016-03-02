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
#include "md5.h"

xarray_p<HttpAuth> HttpAuth::cache;

HttpAuth::Challenge::Challenge(const char *p_chal)
   : scheme_code(HttpAuth::NONE)
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
      xstring& key=xstring::get_tmp(auth_param,eq-auth_param).c_lc();
      SetParam(key,HttpHeader::extract_quoted_value(eq+1,&space));
      while(space<end && (*space==' ' || *space==','))
	 ++space;
      auth_param=space;
   }

   if(scheme.eq("Basic"))
      scheme_code=HttpAuth::BASIC;
   else if(scheme.eq("Digest"))
      scheme_code=HttpAuth::DIGEST;
}

bool HttpAuth::New(target_t t,const char *p_uri,Challenge *p_chal,const char *p_user,const char *p_pass)
{
   Ref<Challenge> chal(p_chal);
   Ref<HttpAuth> auth;
   switch(chal->GetSchemeCode()) {
   case BASIC:
      auth=new HttpAuthBasic(t,p_uri,chal.borrow(),p_user,p_pass);
      break;
   case DIGEST:
      auth=new HttpAuthDigest(t,p_uri,chal.borrow(),p_user,p_pass);
      break;
   case NONE:
      return false;
   }
   if(!auth->IsValid())
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

xstring& HttpAuth::append_quoted(xstring& s,const char *n,const char *v)
{
   if(!v)
      return s;
   if(s.length()>0 && s.last_char()!=' ')
      s.append(',');
   s.append(n).append('=');
   return HttpHeader::append_quoted_value(s,v);
}

void HttpAuthBasic::MakeHeader()
{
   xstring& auth=xstring::get_tmp(user).append(':').append(pass);
   char *buf64=string_alloca(base64_length(auth.length())+1);
   base64_encode(auth,buf64,auth.length());
   header.SetValue(auth.set("Basic ").append(buf64));
}

void HttpAuthDigest::MakeHA1()
{
   const xstring& realm=chal->GetParam("realm");
   const xstring& nonce=chal->GetParam("nonce");
   if(!realm || !nonce)
      return; // required

   // generate random client nonce
   cnonce.truncate();
   for(int i=0; i<8; i++)
      cnonce.appendf("%02x",unsigned(random()/13%256));

   struct md5_ctx ctx;
   md5_init_ctx (&ctx);
   md5_process_bytes (user, user.length(), &ctx);
   md5_process_bytes (":", 1, &ctx);
   md5_process_bytes (realm, realm.length(), &ctx);
   md5_process_bytes (":", 1, &ctx);
   md5_process_bytes (pass, pass.length(), &ctx);

   xstring buf;
   buf.get_space(MD5_DIGEST_SIZE);
   md5_finish_ctx (&ctx, buf.get_non_const());
   buf.set_length(MD5_DIGEST_SIZE);

   if(chal->GetParam("algorithm").eq("MD5-sess")) {
      md5_init_ctx (&ctx);
      md5_process_bytes (buf, buf.length(), &ctx);
      md5_process_bytes (":", 1, &ctx);
      md5_process_bytes (nonce, nonce.length(), &ctx);
      md5_process_bytes (":", 1, &ctx);
      md5_process_bytes (cnonce, cnonce.length(), &ctx);
      md5_finish_ctx (&ctx, buf.get_non_const());
   }

   // lower-case hex encoding
   HA1.truncate();
   buf.hexdump_to(HA1);
   HA1.c_lc();
}

bool HttpAuthDigest::Update(const char *p_method,const char *p_uri,const char *entity_hash)
{
   const xstring& qop_options=chal->GetParam("qop");
   xstring qop;
   if(qop_options) {
      // choose qop
      char *qop_options_split=alloca_strdup(qop_options);
      for(char *qop1=strtok(qop_options_split,","); qop1; qop1=strtok(NULL,",")) {
	 if(!strcmp(qop1,"auth-int") &&	entity_hash) {
	    qop.set(qop1);
	    break;
	 }
	 if(!strcmp(qop1,"auth")) {
	    qop.set(qop1);
	    if(!entity_hash)
	       break;
	 }
      }
   }
   if(qop_options && !qop)
      return false;  // no suitable qop found

   // calculate H(A2)
   struct md5_ctx ctx;
   md5_init_ctx (&ctx);
   md5_process_bytes (p_method, strlen(p_method), &ctx);
   md5_process_bytes (":", 1, &ctx);
   md5_process_bytes (p_uri, strlen(p_uri), &ctx);

   if(qop.eq("auth-int")) {
      md5_process_bytes (":", 1, &ctx);
      md5_process_bytes (entity_hash, strlen(entity_hash), &ctx);
   };

   xstring buf;
   buf.get_space(MD5_DIGEST_SIZE);
   md5_finish_ctx (&ctx, buf.get_non_const());
   buf.set_length(MD5_DIGEST_SIZE);
   xstring HA2;
   buf.hexdump_to(HA2);
   HA2.c_lc();

   // calculate response
   md5_init_ctx (&ctx);
   md5_process_bytes (HA1, HA1.length(), &ctx);
   md5_process_bytes (":", 1, &ctx);
   const xstring& nonce=chal->GetParam("nonce");
   md5_process_bytes (nonce, nonce.length(), &ctx);
   md5_process_bytes (":", 1, &ctx);
   char nc_buf[9];
   if(qop) {
      sprintf(nc_buf,"%08x",++nc);
      md5_process_bytes (nc_buf, strlen(nc_buf), &ctx);
      md5_process_bytes (":", 1, &ctx);
      md5_process_bytes (cnonce, cnonce.length(), &ctx);
      md5_process_bytes (":", 1, &ctx);
      md5_process_bytes (qop, qop.length(), &ctx);
      md5_process_bytes (":", 1, &ctx);
   }
   md5_process_bytes (HA2, HA2.length(), &ctx);
   md5_finish_ctx (&ctx, buf.get_non_const());
   xstring digest;
   buf.hexdump_to(digest);
   digest.c_lc();

   xstring auth("Digest ");
   append_quoted(auth,"username",user);
   append_quoted(auth,"realm",chal->GetParam("realm"));
   append_quoted(auth,"nonce",nonce);
   append_quoted(auth,"uri",p_uri);
   append_quoted(auth,"response",digest);

   append_quoted(auth,"algorithm",chal->GetParam("algorithm"));
   append_quoted(auth,"opaque",chal->GetParam("opaque"));

   if(qop) {
      auth.append(",qop=").append(qop);
      append_quoted(auth,"cnonce",cnonce);
      auth.append(",nc=").append(nc_buf);
   }

   header.SetValue(auth);
   return true;
}
