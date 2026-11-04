#include "detail.hpp"
#include <array>

// Supported named character references: the 252 HTML 4.01 / XHTML 1.0 entities
// (Latin-1, symbols and Greek, special), plus &apos; and the uppercase HTML5
// aliases &AMP; &LT; &GT; &QUOT; &COPY; &REG;. &lang; and &rang; take their
// HTML5 values (U+27E8/U+27E9). Exactly the HTML5 "legacy" names (Latin-1 plus
// amp/lt/gt/quot and the six uppercase aliases) are also recognized without
// the terminating semicolon. Every other name is left as literal text.
namespace arken::markup::v1::detail {
namespace {
struct Entity {std::string_view name;char32_t value;bool legacy;};

constexpr std::array<std::string_view,96> latin1={
    "nbsp","iexcl","cent","pound","curren","yen","brvbar","sect","uml","copy","ordf","laquo","not","shy","reg","macr",
    "deg","plusmn","sup2","sup3","acute","micro","para","middot","cedil","sup1","ordm","raquo","frac14","frac12","frac34","iquest",
    "Agrave","Aacute","Acirc","Atilde","Auml","Aring","AElig","Ccedil","Egrave","Eacute","Ecirc","Euml","Igrave","Iacute","Icirc","Iuml",
    "ETH","Ntilde","Ograve","Oacute","Ocirc","Otilde","Ouml","times","Oslash","Ugrave","Uacute","Ucirc","Uuml","Yacute","THORN","szlig",
    "agrave","aacute","acirc","atilde","auml","aring","aelig","ccedil","egrave","eacute","ecirc","euml","igrave","iacute","icirc","iuml",
    "eth","ntilde","ograve","oacute","ocirc","otilde","ouml","divide","oslash","ugrave","uacute","ucirc","uuml","yacute","thorn","yuml"};

constexpr Entity others[]={
    {"quot",34,true},{"amp",38,true},{"lt",60,true},{"gt",62,true},{"apos",39,false},
    {"QUOT",34,true},{"AMP",38,true},{"LT",60,true},{"GT",62,true},{"COPY",169,true},{"REG",174,true},
    {"OElig",338,false},{"oelig",339,false},{"Scaron",352,false},{"scaron",353,false},{"Yuml",376,false},
    {"fnof",402,false},{"circ",710,false},{"tilde",732,false},
    {"Alpha",913,false},{"Beta",914,false},{"Gamma",915,false},{"Delta",916,false},{"Epsilon",917,false},
    {"Zeta",918,false},{"Eta",919,false},{"Theta",920,false},{"Iota",921,false},{"Kappa",922,false},
    {"Lambda",923,false},{"Mu",924,false},{"Nu",925,false},{"Xi",926,false},{"Omicron",927,false},
    {"Pi",928,false},{"Rho",929,false},{"Sigma",931,false},{"Tau",932,false},{"Upsilon",933,false},
    {"Phi",934,false},{"Chi",935,false},{"Psi",936,false},{"Omega",937,false},
    {"alpha",945,false},{"beta",946,false},{"gamma",947,false},{"delta",948,false},{"epsilon",949,false},
    {"zeta",950,false},{"eta",951,false},{"theta",952,false},{"iota",953,false},{"kappa",954,false},
    {"lambda",955,false},{"mu",956,false},{"nu",957,false},{"xi",958,false},{"omicron",959,false},
    {"pi",960,false},{"rho",961,false},{"sigmaf",962,false},{"sigma",963,false},{"tau",964,false},
    {"upsilon",965,false},{"phi",966,false},{"chi",967,false},{"psi",968,false},{"omega",969,false},
    {"thetasym",977,false},{"upsih",978,false},{"piv",982,false},
    {"ensp",8194,false},{"emsp",8195,false},{"thinsp",8201,false},{"zwnj",8204,false},{"zwj",8205,false},
    {"lrm",8206,false},{"rlm",8207,false},{"ndash",8211,false},{"mdash",8212,false},{"lsquo",8216,false},
    {"rsquo",8217,false},{"sbquo",8218,false},{"ldquo",8220,false},{"rdquo",8221,false},{"bdquo",8222,false},
    {"dagger",8224,false},{"Dagger",8225,false},{"bull",8226,false},{"hellip",8230,false},{"permil",8240,false},
    {"prime",8242,false},{"Prime",8243,false},{"lsaquo",8249,false},{"rsaquo",8250,false},{"oline",8254,false},
    {"frasl",8260,false},{"euro",8364,false},{"image",8465,false},{"weierp",8472,false},{"real",8476,false},
    {"trade",8482,false},{"alefsym",8501,false},{"larr",8592,false},{"uarr",8593,false},{"rarr",8594,false},
    {"darr",8595,false},{"harr",8596,false},{"crarr",8629,false},{"lArr",8656,false},{"uArr",8657,false},
    {"rArr",8658,false},{"dArr",8659,false},{"hArr",8660,false},{"forall",8704,false},{"part",8706,false},
    {"exist",8707,false},{"empty",8709,false},{"nabla",8711,false},{"isin",8712,false},{"notin",8713,false},
    {"ni",8715,false},{"prod",8719,false},{"sum",8721,false},{"minus",8722,false},{"lowast",8727,false},
    {"radic",8730,false},{"prop",8733,false},{"infin",8734,false},{"ang",8736,false},{"and",8743,false},
    {"or",8744,false},{"cap",8745,false},{"cup",8746,false},{"int",8747,false},{"there4",8756,false},
    {"sim",8764,false},{"cong",8773,false},{"asymp",8776,false},{"ne",8800,false},{"equiv",8801,false},
    {"le",8804,false},{"ge",8805,false},{"sub",8834,false},{"sup",8835,false},{"nsub",8836,false},
    {"sube",8838,false},{"supe",8839,false},{"oplus",8853,false},{"otimes",8855,false},{"perp",8869,false},
    {"sdot",8901,false},{"lceil",8968,false},{"rceil",8969,false},{"lfloor",8970,false},{"rfloor",8971,false},
    {"lang",0x27e8,false},{"rang",0x27e9,false},{"loz",9674,false},{"spades",9824,false},{"clubs",9827,false},
    {"hearts",9829,false},{"diams",9830,false},
};

const std::vector<Entity>& Table() {
    static const std::vector<Entity> table=[] {
        std::vector<Entity> t;
        for(std::size_t i=0;i<latin1.size();++i)t.push_back({latin1[i],char32_t(160+i),true});
        for(const auto& e:others)t.push_back(e);
        std::sort(t.begin(),t.end(),[](const Entity& a,const Entity& b) {return a.name<b.name;});
        return t;
    }();
    return table;
}
}

char32_t LookupEntity(std::string_view name,bool* legacy) noexcept {
    const auto& t=Table();
    const auto it=std::lower_bound(t.begin(),t.end(),name,[](const Entity& e,std::string_view n) {return e.name<n;});
    if(it==t.end()||it->name!=name)return 0;
    if(legacy)*legacy=it->legacy;
    return it->value;
}

std::size_t LongestLegacyEntity(std::string_view s,char32_t& value) noexcept {
    // Legacy names are at most 6 characters long.
    for(std::size_t n=std::min<std::size_t>(s.size(),6);n>=2;--n) {
        bool legacy=false;
        const auto v=LookupEntity(s.substr(0,n),&legacy);
        if(v&&legacy) {value=v;return n;}
    }
    return 0;
}

char32_t NumericReference(std::uint64_t value) noexcept {
    static constexpr char32_t windows1252[32]={
        0x20AC,0xFFFD,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0xFFFD,0x017D,0xFFFD,
        0xFFFD,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0xFFFD,0x017E,0x0178};
    if(value==0||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return 0xfffd;
    if(value>=0x80&&value<=0x9f)return windows1252[value-0x80];
    if((value<0x20&&value!='\t'&&value!='\n'&&value!='\f')||value==0x7f)return 0xfffd;
    return char32_t(value);
}
}
