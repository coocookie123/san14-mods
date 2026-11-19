#pragma once
// Bounded JSON reader for plugin manifests. No execution, comments or duplicate keys.
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
namespace json {
struct Value {
    enum Type {Null,Bool,Number,String,Array,Object} type=Null;
    bool boolean=false;long long number=0;std::string string;
    std::vector<Value> array;std::map<std::string,Value> object;
    const Value& at(const char* k) const {auto i=object.find(k);if(type!=Object || i==object.end())throw std::runtime_error(std::string("Missing field: ")+k);return i->second;}
    std::string str() const {if(type!=String)throw std::runtime_error("Expected string");return string;}
    long long integer() const {if(type!=Number)throw std::runtime_error("Expected integer");return number;}
    bool flag() const {if(type!=Bool)throw std::runtime_error("Expected boolean");return boolean;}
    bool has(const char* k) const {return object.count(k)!=0;}
};
class Parser {
    const std::string& s;size_t p=0;
    [[noreturn]] void fail() {throw std::runtime_error("Invalid JSON");}
    void ws(){while(p<s.size() && (s[p]==' ' || s[p]=='\r' || s[p]=='\n' || s[p]=='\t'))++p;}
    char get(){if(p>=s.size())fail();return s[p++];}
    unsigned hex4(){unsigned n=0;for(int i=0;i<4;i++){char c=get();n<<=4;if(c>='0'&&c<='9')n+=c-'0';else if(c>='a'&&c<='f')n+=c-'a'+10;else if(c>='A'&&c<='F')n+=c-'A'+10;else fail();}return n;}
    void utf8(std::string& o,unsigned c){if(c<=0x7f)o+=char(c);else if(c<0x800){o+=char(0xc0|(c>>6));o+=char(0x80|(c&63));}else if(c<0x10000){o+=char(0xe0|(c>>12));o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}else{o+=char(0xf0|(c>>18));o+=char(0x80|((c>>12)&63));o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}}
    std::string string(){if(get()!='"')fail();std::string o;for(;;){unsigned char c=get();if(c=='"')return o;if(c<32)fail();if(c!='\\'){o+=char(c);continue;}switch(get()){case '"':o+='"';break;case '\\':o+='\\';break;case '/':o+='/';break;case 'b':o+='\b';break;case 'f':o+='\f';break;case 'n':o+='\n';break;case 'r':o+='\r';break;case 't':o+='\t';break;case 'u':{unsigned x=hex4();if(x>=0xd800&&x<=0xdbff){if(get()!='\\'||get()!='u')fail();unsigned y=hex4();if(y<0xdc00||y>0xdfff)fail();x=0x10000+((x-0xd800)<<10)+(y-0xdc00);}else if(x>=0xdc00&&x<=0xdfff)fail();utf8(o,x);break;}default:fail();}}}
    Value value(unsigned depth){if(depth>24)fail();ws();Value v;char c=get();if(c=='"'){--p;v.type=Value::String;v.string=string();return v;}if(c=='{'||c=='['){v.type=c=='{'?Value::Object:Value::Array;ws();if(p<s.size()&&s[p]==(c=='{'?'}':']')){++p;return v;}for(;;){std::string key;if(c=='{'){ws();key=string();ws();if(get()!=':')fail();}Value x=value(depth+1);if(c=='{'){if(!v.object.emplace(key,std::move(x)).second)fail();}else v.array.push_back(std::move(x));ws();char end=get();if(end==(c=='{'?'}':']'))return v;if(end!=',')fail();}}if(c=='t'||c=='f'||c=='n'){const char* tail=c=='t'?"rue":c=='f'?"alse":"ull";while(*tail)if(get()!=*tail++)fail();v.type=c=='n'?Value::Null:Value::Bool;v.boolean=c=='t';return v;}if(c=='-'||(c>='0'&&c<='9')){size_t start=p-1;if(c=='-'){c=get();if(c<'0'||c>'9')fail();}if(c=='0'&&p<s.size()&&std::isdigit((unsigned char)s[p]))fail();while(p<s.size()&&std::isdigit((unsigned char)s[p]))++p;if(p-start>16)fail();v.type=Value::Number;v.number=std::stoll(s.substr(start,p-start));return v;}fail();}
public:
    explicit Parser(const std::string& text):s(text){}
    Value parse(){if(s.size()>1024*1024)fail();if(s.size()>=3&&(unsigned char)s[0]==0xef&&(unsigned char)s[1]==0xbb&&(unsigned char)s[2]==0xbf)p=3;Value v=value(0);ws();if(p!=s.size())fail();return v;}
};
inline Value parse(const std::string& s){return Parser(s).parse();}
}
