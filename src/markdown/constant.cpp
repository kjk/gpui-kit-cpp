/* src/util/constant.rs — the tables.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).
   Two of the crate's three lists, transcribed. The third,
   CHARACTER_REFERENCES, is the WHATWG table html5ever holds too, so it lives
   once in base (`base::HtmlNamedCharRef`, src/base_entities.cpp). */

#include "markdown/constant.h"

namespace markdown {

// The characters after `<![` that open a CDATA section.
extern const Str kHtmlCdataPrefix = StrL("CDATA[");

// The tag names that open an HTML (flow) block of kind 6.
const char kHtmlBlockNames[] =
    "address\0"
    "article\0"
    "aside\0"
    "base\0"
    "basefont\0"
    "blockquote\0"
    "body\0"
    "caption\0"
    "center\0"
    "col\0"
    "colgroup\0"
    "dd\0"
    "details\0"
    "dialog\0"
    "dir\0"
    "div\0"
    "dl\0"
    "dt\0"
    "fieldset\0"
    "figcaption\0"
    "figure\0"
    "footer\0"
    "form\0"
    "frame\0"
    "frameset\0"
    "h1\0"
    "h2\0"
    "h3\0"
    "h4\0"
    "h5\0"
    "h6\0"
    "head\0"
    "header\0"
    "hr\0"
    "html\0"
    "iframe\0"
    "legend\0"
    "li\0"
    "link\0"
    "main\0"
    "menu\0"
    "menuitem\0"
    "nav\0"
    "noframes\0"
    "ol\0"
    "optgroup\0"
    "option\0"
    "p\0"
    "param\0"
    "search\0"
    "section\0"
    "summary\0"
    "table\0"
    "tbody\0"
    "td\0"
    "tfoot\0"
    "th\0"
    "thead\0"
    "title\0"
    "tr\0"
    "track\0"
    "ul\0";

// The tag names that open an HTML (flow) block of kind 1, which
// runs as raw text until its closing tag.
const char kHtmlRawNames[] =
    "pre\0"
    "script\0"
    "style\0"
    "textarea\0";

} // namespace markdown
