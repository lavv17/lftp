/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "sitemgr.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include "trio.h"
#include "ResMgr.h"
#include "misc.h"
#include "url.h"

#include <json-c/json.h>

Sitemgr lftp_sitemgr;

Sitemgr::SiteList sites;

Sitemgr::Sitemgr() {

    const char *home = get_lftp_data_dir();
    if (!home) {
        fprintf(stderr, "Home directory not found. Aborting.");
        return;
    }
    sites_file.vset(home, "/sites", NULL);
    Load();

}

Sitemgr::~Sitemgr() {
    Clear();
}

void Sitemgr::Refresh() {
    Clear();
    Load();
}

void Sitemgr::Clear() {
    Site *current, *prev;

    current = sites.tail;
    while (current != NULL) {
        prev = current->prev;
        FreeSite(current);
        current = prev;
    }

    sites.head = NULL;
    sites.tail = NULL;
    sites.size = 0;
}

void Sitemgr::Load() {
    int site_count, i;
    struct json_object *json_sites, *temp_site;
    json_sites = json_object_from_file(sites_file);

    if (json_sites) {
        site_count = json_object_array_length(json_sites);
        for (i = 0; i < site_count; i++) {
            temp_site = json_object_array_get_idx(json_sites, i);
            AppendSite(JsonObj2Site(temp_site));
        }
    }
}

void Sitemgr::AppendSite(Site *site) {

    if (!sites.head) {
        sites.head = site;
        sites.tail = site;
        sites.size = 1;
    } else {
        Site *old_tail = sites.tail;
        old_tail->next = site;
        sites.tail = site;
        site->prev = old_tail;
        sites.size++;
    }
}

void Sitemgr::PreModify() {

}

void Sitemgr::PostModify() {

}

void Sitemgr::Add(const char *sitename) {
    if (SiteExists(sitename)) {
        fprintf(stderr, "Site already exists: %s\n", sitename);
        return;
    }

    // Initialize site.
    Site *new_site = (Site*) malloc(sizeof (Site));
    new_site->sitename = strdup(sitename);
    InitializeSite(new_site);
    AppendSite(new_site);
}

void Sitemgr::Remove(const char *sitename) {

    if (!SiteExists(sitename)) {
        fprintf(stderr, "Site does not exists: %s\n", sitename);
        return;
    }

    Site *current_site = sites.head;
    Site *previous_site, *next_site;

    while (current_site != NULL) {

        if (strcasecmp(sitename, current_site->sitename) == 0) {
            previous_site = current_site->prev;
            next_site = current_site->next;

            if (current_site == sites.head)
                sites.head = next_site;

            if (next_site)
                next_site->prev = previous_site;

            if (previous_site)
                previous_site->next = next_site;

            FreeSite(current_site);
            return;
        }
        current_site = current_site->next;
    }
    sites.size--;
}

void Sitemgr::FreeSite(Site *site) {
    free(site->address);
    free(site->localPath);
    free(site->notes);
    free(site->password);
    free(site->port);
    free(site->remotePath);
    free(site->sitename);
    free(site->username);
    free(site);
}

char *Sitemgr::List() {

    xstring buf("");

    Site *current_site = sites.head;
    buf.append("Listing sites: \n");
    while (current_site != NULL) {
        buf.appendf("   - Site: %s ( %s:%s | %s )\n", current_site->sitename, current_site->address, current_site->port, current_site->notes);
        current_site = current_site->next;
    }
    return buf.borrow();
}

char *Sitemgr::ListParsable(const char* delimiter) {
    xstring buf("");

    Site *current_site = sites.head;
    while (current_site != NULL) {
        buf.appendf("%s%s", current_site->sitename, delimiter);
        current_site = current_site->next;
    }
    return buf.borrow();
}

Sitemgr::Site *Sitemgr::SiteAt(int position) {
    Site *current_site = sites.head;
    for (int i = 1; i <= position; i++) {
        if (current_site->next != NULL) {
            current_site = current_site->next;
        } else {
            return NULL;
        }
    }
    return current_site;
}

bool Sitemgr::SiteExists(const char *sitename) {
    Site *current_site = sites.head;
    while (current_site != NULL) {

        if (strcasecmp(sitename, current_site->sitename) == 0) {
            return true;
        }

        current_site = current_site->next;
    }
    return false;
}

Sitemgr::Site *Sitemgr::JsonObj2Site(json_object *site_obj) {

    Site *new_site = (Site*) malloc(sizeof (Site));
    InitializeSite(new_site);

    json_object *temp_value;
    json_object_object_get_ex(site_obj, "Sitename", &temp_value);
    new_site->sitename = strdup(json_object_get_string(temp_value));
    //
    json_object_object_get_ex(site_obj, "Address", &temp_value);
    new_site->address = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "Port", &temp_value);
    new_site->port = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "Notes", &temp_value);
    new_site->notes = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "Username", &temp_value);
    new_site->username = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "Password", &temp_value);
    new_site->password = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "LocalPath", &temp_value);
    new_site->localPath = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "RemotePath", &temp_value);
    new_site->remotePath = strdup(json_object_get_string(temp_value));

    json_object_object_get_ex(site_obj, "SSL", &temp_value);
    new_site->ssl = json_object_get_boolean(temp_value);

    return new_site;
}

json_object *Sitemgr::Site2JsonObj(const char *sitename) {
    struct json_object *json_site, *json_tmp;

    Site *site = GetSite(sitename);

    json_site = json_object_new_object();

    // Sitename
    json_tmp = json_object_new_string(site->sitename);
    json_object_object_add(json_site, "Sitename", json_tmp);

    // Address
    json_tmp = json_object_new_string(site->address);
    json_object_object_add(json_site, "Address", json_tmp);

    // Port
    json_tmp = json_object_new_string(site->port);
    json_object_object_add(json_site, "Port", json_tmp);

    // Notes
    json_tmp = json_object_new_string(site->notes);
    json_object_object_add(json_site, "Notes", json_tmp);

    // User
    json_tmp = json_object_new_string(site->username);
    json_object_object_add(json_site, "Username", json_tmp);

    // User
    json_tmp = json_object_new_string(site->password);
    json_object_object_add(json_site, "Password", json_tmp);

    // User
    json_tmp = json_object_new_string(site->localPath);
    json_object_object_add(json_site, "LocalPath", json_tmp);

    // User
    json_tmp = json_object_new_string(site->remotePath);
    json_object_object_add(json_site, "RemotePath", json_tmp);

    // SSL
    json_tmp = json_object_new_boolean(site->ssl);
    json_object_object_add(json_site, "SSL", json_tmp);
    // const char* jsonString = json_object_get_string(jsite);
    // fprintf(stdout, "Return string.\n");
    return json_site;
}

const char *Sitemgr::Site2JsonString(const char *sitename) {
    return json_object_to_json_string_ext(Site2JsonObj(sitename), JSON_C_TO_STRING_PRETTY);
}

json_object *Sitemgr::SiteList2JsonObj() {
    struct json_object *json_sites;

    json_sites = json_object_new_array();

    Site *current_site = sites.head;
    while (current_site != NULL) {
        json_object_array_add(json_sites, Site2JsonObj(current_site->sitename));
        current_site = current_site->next;
    }
    return json_sites;
}

const char *Sitemgr::SiteList2JsonString() {
    return json_object_get_string(SiteList2JsonObj());
}

Sitemgr::Site *Sitemgr::GetSite(const char *sitename) {

    Sitemgr::Site *current_site = sites.head;
    while (current_site != NULL) {
        if (strcasecmp(sitename, current_site->sitename) == 0) {
            return current_site;
        }
        current_site = current_site->next;
    }
    return NULL;
}

void Sitemgr::Save() {
    json_object_to_file(sites_file.get_non_const(), SiteList2JsonObj());
}

char *Sitemgr::Conf(const char* sitename) {

    if (!SiteExists(sitename)) {
        fprintf(stderr, "Site does not exist: %s", sitename);
        return NULL;
    }
    xstring buf("");
    // dump whole structure.
    // buf.appendf("%s\n", json_object_to_json_string_ext(Site2JsonObj(sitename), JSON_C_TO_STRING_PRETTY));
    Site* site = GetSite(sitename);
    buf.appendf("  Configuration for - [ %s ] -\n", site->sitename );
    buf.append ("---------------------------------------\n");
    buf.appendf("  User         - %s\n", site->username );
    buf.appendf("  Password     - %s\n", site->password );
    buf.appendf("  Address      - %s\n", site->address );
    buf.appendf("  Port         - %s\n", site->port );
    if (site->ssl)
        buf.appendf("  SSL          - %s\n", "Yes" );
    else
        buf.appendf("  SSL          - %s\n", "No" );
    buf.append ("----------------------------------------\n");
    buf.appendf("  Notes        - %s\n", site->notes );
    buf.appendf("  Local Path   - %s\n", site->localPath );
    buf.appendf("  Remote Path  - %s\n", site->remotePath );
    buf.append ("----------------------------------------\n");    
    return buf.borrow();
}

void Sitemgr::ConfSet(const char* sitename, const char* field, const char *value) {

    if (!SiteExists(sitename)) {
        fprintf(stderr, "Site does not exist: %s", sitename);
        return;
    }

    Site* site = GetSite(sitename);
    if (strcasecmp(field, "sitename") == 0) {
        site->sitename = strdup(value);
    } else if (strcasecmp(field, "address") == 0) {
        site->address = strdup(value);
    } else if (strcasecmp(field, "port") == 0) {
        site->port = strdup(value);
    } else if (strcasecmp(field, "notes") == 0) {
        site->notes = strdup(value);
    } else if ((strcasecmp(field, "user") == 0) || (strcasecmp(field, "username") == 0)) {
        site->username = strdup(value);
    } else if ((strcasecmp(field, "pass") == 0) || (strcasecmp(field, "password") == 0)) {
        site->password = strdup(value);
    } else if (strcasecmp(field, "localPath") == 0) {
        site->localPath = strdup(value);
    } else if (strcasecmp(field, "remotePath") == 0) {
        site->remotePath = strdup(value);
    } else if (strcasecmp(field, "ssl") == 0) {
        if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "1") == 0) {
            site->ssl = true;
        }
        if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "0") == 0) {
            site->ssl = false;
        }
    }
}

void Sitemgr::InitializeSite(Site* site) {
    site->prev = NULL;
    site->next = NULL;
    site->address = strdup("");
    site->username = strdup("anonymous");
    site->password = strdup("");
    site->port = strdup("21");
    site->notes = strdup("");
    site->localPath = strdup("");
    site->remotePath = strdup("/");
    site->ssl = true;
}

int Sitemgr::SiteCount() {
    return sites.size;
}