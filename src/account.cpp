#include "realm/account.hpp"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace realm {
namespace {
struct Document { xmlDoc* doc; ~Document() { xmlFreeDoc(doc); } };
std::string_view name(const xmlNode* node) { return reinterpret_cast<const char*>(node->name); }
const xmlNode* child(const xmlNode* node,std::string_view tag) {
    for(auto* c=node ? node->children : nullptr;c;c=c->next)if(c->type==XML_ELEMENT_NODE&&name(c)==tag)return c;
    return nullptr;
}
std::string text(const xmlNode* node) {
    if(!node)return {};
    auto* content=xmlNodeGetContent(node);std::string result=content ? reinterpret_cast<const char*>(content) : "";xmlFree(content);
    return result;
}
std::string attribute(const xmlNode* node,const char* key) {
    auto* value=xmlGetProp(node,reinterpret_cast<const xmlChar*>(key));std::string result=value ? reinterpret_cast<const char*>(value) : "";xmlFree(value);
    return result;
}
// AS3 int(String): unparsable values become zero.
int integer(const std::string& value) { char* end{};const auto result=std::strtol(value.c_str(),&end,0);return end==value.c_str() ? 0 : static_cast<int>(result); }
int integer(const xmlNode* node) { return integer(text(node)); }
std::unique_ptr<Document> parse(std::string_view source) {
    if(source.size()>8*1024*1024)throw std::runtime_error("AppEngine XML exceeds limit");
    auto* doc=xmlReadMemory(source.data(),static_cast<int>(source.size()),nullptr,nullptr,XML_PARSE_NONET|XML_PARSE_NOBLANKS|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
    if(!doc)throw std::runtime_error("Malformed AppEngine XML");
    return std::unique_ptr<Document>(new Document{doc});
}
}
int star_count(int fame) { int n{};while(n<5&&fame>=fame_stars[n])++n;return n; }
int next_star_fame(int best,int current) { const int value=std::max(best,current);for(const int star:fame_stars)if(star>value)return star;return -1; }
int CharList::stars() const { int total{};for(const auto& entry:best_fame)total+=star_count(entry.second);return total; }
std::optional<std::string> app_error(std::string_view response) {
    const auto start=response.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if(start==std::string_view::npos||response.compare(start,6,"<Error")!=0)return std::nullopt;
    try { const auto doc=parse(response);return text(xmlDocGetRootElement(doc->doc)); }
    catch(const std::exception&) { return std::string(response.substr(start)); }
}
CharList parse_char_list(std::string_view source) {
    if(auto error=app_error(source))throw std::runtime_error(*error);
    const auto doc=parse(source);const auto* root=xmlDocGetRootElement(doc->doc);
    if(!root||name(root)!="Chars")throw std::runtime_error("Character list reply has no <Chars> root");
    CharList list;
    list.next_char_id=integer(attribute(root,"nextCharId"));list.max_num_chars=integer(attribute(root,"maxNumChars"));
    const auto* account=child(root,"Account");
    list.account_id=integer(child(account,"AccountId"));list.name=text(child(account,"Name"));
    const auto* stats=child(account,"Stats");
    list.total_fame=integer(child(stats,"TotalFame"));list.fame=integer(child(stats,"Fame"));list.credits=integer(child(stats,"Credits"));
    for(auto* c=stats ? stats->children : nullptr;c;c=c->next)
        if(c->type==XML_ELEMENT_NODE&&name(c)=="ClassStats")list.best_fame[integer(attribute(c,"objectType"))]=integer(child(c,"BestFame"));
    for(auto* c=root->children;c;c=c->next) {
        if(c->type!=XML_ELEMENT_NODE||name(c)!="Char")continue;
        SavedCharacter character;
        character.id=integer(attribute(c,"id"));character.object_type=integer(child(c,"ObjectType"));character.level=integer(child(c,"Level"));
        character.experience=integer(child(c,"Exp"));character.fame=integer(child(c,"CurrentFame"));character.skin=integer(child(c,"Texture"));
        character.tex1=integer(child(c,"Tex1"));character.tex2=integer(child(c,"Tex2"));
        list.characters.push_back(character);
    }
    for(auto* c=child(root,"News") ? child(root,"News")->children : nullptr;c;c=c->next) {
        if(c->type!=XML_ELEMENT_NODE||name(c)!="Item")continue;
        list.news.push_back({text(child(c,"Icon")),text(child(c,"Title")),text(child(c,"TagLine")),text(child(c,"Link")),std::strtoll(text(child(c,"Date")).c_str(),nullptr,10)});
    }
    return list;
}
void sort_characters(CharList& list,const std::map<int,std::int64_t>& use) {
    const auto used=[&](int id) { const auto found=use.find(id);return found==use.end() ? std::int64_t{} : found->second; };
    std::stable_sort(list.characters.begin(),list.characters.end(),[&](const auto& a,const auto& b) {
        if(used(a.id)!=used(b.id))return used(a.id)>used(b.id);
        return a.experience>b.experience;
    });
}
std::filesystem::path account_path() {
#ifdef _WIN32
    if(const char* appdata=std::getenv("APPDATA"))return std::filesystem::path(appdata)/"realm-cpp"/"account.json";
#else
    if(const char* config=std::getenv("XDG_CONFIG_HOME");config&&*config)return std::filesystem::path(config)/"realm-cpp"/"account.json";
    if(const char* home=std::getenv("HOME"))return std::filesystem::path(home)/".config"/"realm-cpp"/"account.json";
#endif
    return "realm-account.json";
}
SavedAccount load_account(const std::filesystem::path& path) {
    SavedAccount account;
    std::ifstream file(path);if(!file)return account;
    try {
        const auto json=nlohmann::json::parse(file);
        account.username=json.value("username",std::string{});account.password=json.value("password",std::string{});
        if(const auto use=json.find("charIdUseMap");use!=json.end()&&use->is_object())
            for(const auto& [id,time]:use->items())if(time.is_number_integer())account.char_use[std::stoi(id)]=time.get<std::int64_t>();
    } catch(const std::exception&) { return {}; }
    return account;
}
void save_account(const std::filesystem::path& path,const SavedAccount& account) {
    nlohmann::json json{{"username",account.username},{"password",account.password},{"charIdUseMap",nlohmann::json::object()}};
    for(const auto& [id,time]:account.char_use)json["charIdUseMap"][std::to_string(id)]=time;
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);
    const auto temporary=path.string()+".tmp";
    {
        std::ofstream file(temporary,std::ios::trunc);
        if(!file)throw std::runtime_error("Cannot write account file "+temporary);
        file<<json.dump(2)<<'\n';
    }
    std::filesystem::permissions(temporary,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,error);
    std::filesystem::rename(temporary,path);
}
}
