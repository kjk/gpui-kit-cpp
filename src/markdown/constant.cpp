/* src/util/constant.rs — the tables.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).
   These are the crate's three lists, transcribed. Sources here are UTF-8 and
   the build passes /utf-8, so a character reference's value is the same
   literal the Rust holds. */

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

// Every named character reference of HTML 5, sensitive to casing.
//
// The crate walks this list with `find`, so its order is nearly
// but not quite ascending (`emsp14` before `emsp`, `sup3` before
// `sup`). Here it is sorted, so `DecodeNamed` can binary search
// it; the pairs are the crate's, and only those two move.
// The names, sorted, as one run. The table below indexes into it and
// into the values beside it, so a lookup is still a binary search and
// the tables carry no pointer — and so no relocation — per entry.
const char kCharacterReferenceNames[] =
    "AElig\0"
    "AMP\0"
    "Aacute\0"
    "Abreve\0"
    "Acirc\0"
    "Acy\0"
    "Afr\0"
    "Agrave\0"
    "Alpha\0"
    "Amacr\0"
    "And\0"
    "Aogon\0"
    "Aopf\0"
    "ApplyFunction\0"
    "Aring\0"
    "Ascr\0"
    "Assign\0"
    "Atilde\0"
    "Auml\0"
    "Backslash\0"
    "Barv\0"
    "Barwed\0"
    "Bcy\0"
    "Because\0"
    "Bernoullis\0"
    "Beta\0"
    "Bfr\0"
    "Bopf\0"
    "Breve\0"
    "Bscr\0"
    "Bumpeq\0"
    "CHcy\0"
    "COPY\0"
    "Cacute\0"
    "Cap\0"
    "CapitalDifferentialD\0"
    "Cayleys\0"
    "Ccaron\0"
    "Ccedil\0"
    "Ccirc\0"
    "Cconint\0"
    "Cdot\0"
    "Cedilla\0"
    "CenterDot\0"
    "Cfr\0"
    "Chi\0"
    "CircleDot\0"
    "CircleMinus\0"
    "CirclePlus\0"
    "CircleTimes\0"
    "ClockwiseContourIntegral\0"
    "CloseCurlyDoubleQuote\0"
    "CloseCurlyQuote\0"
    "Colon\0"
    "Colone\0"
    "Congruent\0"
    "Conint\0"
    "ContourIntegral\0"
    "Copf\0"
    "Coproduct\0"
    "CounterClockwiseContourIntegral\0"
    "Cross\0"
    "Cscr\0"
    "Cup\0"
    "CupCap\0"
    "DD\0"
    "DDotrahd\0"
    "DJcy\0"
    "DScy\0"
    "DZcy\0"
    "Dagger\0"
    "Darr\0"
    "Dashv\0"
    "Dcaron\0"
    "Dcy\0"
    "Del\0"
    "Delta\0"
    "Dfr\0"
    "DiacriticalAcute\0"
    "DiacriticalDot\0"
    "DiacriticalDoubleAcute\0"
    "DiacriticalGrave\0"
    "DiacriticalTilde\0"
    "Diamond\0"
    "DifferentialD\0"
    "Dopf\0"
    "Dot\0"
    "DotDot\0"
    "DotEqual\0"
    "DoubleContourIntegral\0"
    "DoubleDot\0"
    "DoubleDownArrow\0"
    "DoubleLeftArrow\0"
    "DoubleLeftRightArrow\0"
    "DoubleLeftTee\0"
    "DoubleLongLeftArrow\0"
    "DoubleLongLeftRightArrow\0"
    "DoubleLongRightArrow\0"
    "DoubleRightArrow\0"
    "DoubleRightTee\0"
    "DoubleUpArrow\0"
    "DoubleUpDownArrow\0"
    "DoubleVerticalBar\0"
    "DownArrow\0"
    "DownArrowBar\0"
    "DownArrowUpArrow\0"
    "DownBreve\0"
    "DownLeftRightVector\0"
    "DownLeftTeeVector\0"
    "DownLeftVector\0"
    "DownLeftVectorBar\0"
    "DownRightTeeVector\0"
    "DownRightVector\0"
    "DownRightVectorBar\0"
    "DownTee\0"
    "DownTeeArrow\0"
    "Downarrow\0"
    "Dscr\0"
    "Dstrok\0"
    "ENG\0"
    "ETH\0"
    "Eacute\0"
    "Ecaron\0"
    "Ecirc\0"
    "Ecy\0"
    "Edot\0"
    "Efr\0"
    "Egrave\0"
    "Element\0"
    "Emacr\0"
    "EmptySmallSquare\0"
    "EmptyVerySmallSquare\0"
    "Eogon\0"
    "Eopf\0"
    "Epsilon\0"
    "Equal\0"
    "EqualTilde\0"
    "Equilibrium\0"
    "Escr\0"
    "Esim\0"
    "Eta\0"
    "Euml\0"
    "Exists\0"
    "ExponentialE\0"
    "Fcy\0"
    "Ffr\0"
    "FilledSmallSquare\0"
    "FilledVerySmallSquare\0"
    "Fopf\0"
    "ForAll\0"
    "Fouriertrf\0"
    "Fscr\0"
    "GJcy\0"
    "GT\0"
    "Gamma\0"
    "Gammad\0"
    "Gbreve\0"
    "Gcedil\0"
    "Gcirc\0"
    "Gcy\0"
    "Gdot\0"
    "Gfr\0"
    "Gg\0"
    "Gopf\0"
    "GreaterEqual\0"
    "GreaterEqualLess\0"
    "GreaterFullEqual\0"
    "GreaterGreater\0"
    "GreaterLess\0"
    "GreaterSlantEqual\0"
    "GreaterTilde\0"
    "Gscr\0"
    "Gt\0"
    "HARDcy\0"
    "Hacek\0"
    "Hat\0"
    "Hcirc\0"
    "Hfr\0"
    "HilbertSpace\0"
    "Hopf\0"
    "HorizontalLine\0"
    "Hscr\0"
    "Hstrok\0"
    "HumpDownHump\0"
    "HumpEqual\0"
    "IEcy\0"
    "IJlig\0"
    "IOcy\0"
    "Iacute\0"
    "Icirc\0"
    "Icy\0"
    "Idot\0"
    "Ifr\0"
    "Igrave\0"
    "Im\0"
    "Imacr\0"
    "ImaginaryI\0"
    "Implies\0"
    "Int\0"
    "Integral\0"
    "Intersection\0"
    "InvisibleComma\0"
    "InvisibleTimes\0"
    "Iogon\0"
    "Iopf\0"
    "Iota\0"
    "Iscr\0"
    "Itilde\0"
    "Iukcy\0"
    "Iuml\0"
    "Jcirc\0"
    "Jcy\0"
    "Jfr\0"
    "Jopf\0"
    "Jscr\0"
    "Jsercy\0"
    "Jukcy\0"
    "KHcy\0"
    "KJcy\0"
    "Kappa\0"
    "Kcedil\0"
    "Kcy\0"
    "Kfr\0"
    "Kopf\0"
    "Kscr\0"
    "LJcy\0"
    "LT\0"
    "Lacute\0"
    "Lambda\0"
    "Lang\0"
    "Laplacetrf\0"
    "Larr\0"
    "Lcaron\0"
    "Lcedil\0"
    "Lcy\0"
    "LeftAngleBracket\0"
    "LeftArrow\0"
    "LeftArrowBar\0"
    "LeftArrowRightArrow\0"
    "LeftCeiling\0"
    "LeftDoubleBracket\0"
    "LeftDownTeeVector\0"
    "LeftDownVector\0"
    "LeftDownVectorBar\0"
    "LeftFloor\0"
    "LeftRightArrow\0"
    "LeftRightVector\0"
    "LeftTee\0"
    "LeftTeeArrow\0"
    "LeftTeeVector\0"
    "LeftTriangle\0"
    "LeftTriangleBar\0"
    "LeftTriangleEqual\0"
    "LeftUpDownVector\0"
    "LeftUpTeeVector\0"
    "LeftUpVector\0"
    "LeftUpVectorBar\0"
    "LeftVector\0"
    "LeftVectorBar\0"
    "Leftarrow\0"
    "Leftrightarrow\0"
    "LessEqualGreater\0"
    "LessFullEqual\0"
    "LessGreater\0"
    "LessLess\0"
    "LessSlantEqual\0"
    "LessTilde\0"
    "Lfr\0"
    "Ll\0"
    "Lleftarrow\0"
    "Lmidot\0"
    "LongLeftArrow\0"
    "LongLeftRightArrow\0"
    "LongRightArrow\0"
    "Longleftarrow\0"
    "Longleftrightarrow\0"
    "Longrightarrow\0"
    "Lopf\0"
    "LowerLeftArrow\0"
    "LowerRightArrow\0"
    "Lscr\0"
    "Lsh\0"
    "Lstrok\0"
    "Lt\0"
    "Map\0"
    "Mcy\0"
    "MediumSpace\0"
    "Mellintrf\0"
    "Mfr\0"
    "MinusPlus\0"
    "Mopf\0"
    "Mscr\0"
    "Mu\0"
    "NJcy\0"
    "Nacute\0"
    "Ncaron\0"
    "Ncedil\0"
    "Ncy\0"
    "NegativeMediumSpace\0"
    "NegativeThickSpace\0"
    "NegativeThinSpace\0"
    "NegativeVeryThinSpace\0"
    "NestedGreaterGreater\0"
    "NestedLessLess\0"
    "NewLine\0"
    "Nfr\0"
    "NoBreak\0"
    "NonBreakingSpace\0"
    "Nopf\0"
    "Not\0"
    "NotCongruent\0"
    "NotCupCap\0"
    "NotDoubleVerticalBar\0"
    "NotElement\0"
    "NotEqual\0"
    "NotEqualTilde\0"
    "NotExists\0"
    "NotGreater\0"
    "NotGreaterEqual\0"
    "NotGreaterFullEqual\0"
    "NotGreaterGreater\0"
    "NotGreaterLess\0"
    "NotGreaterSlantEqual\0"
    "NotGreaterTilde\0"
    "NotHumpDownHump\0"
    "NotHumpEqual\0"
    "NotLeftTriangle\0"
    "NotLeftTriangleBar\0"
    "NotLeftTriangleEqual\0"
    "NotLess\0"
    "NotLessEqual\0"
    "NotLessGreater\0"
    "NotLessLess\0"
    "NotLessSlantEqual\0"
    "NotLessTilde\0"
    "NotNestedGreaterGreater\0"
    "NotNestedLessLess\0"
    "NotPrecedes\0"
    "NotPrecedesEqual\0"
    "NotPrecedesSlantEqual\0"
    "NotReverseElement\0"
    "NotRightTriangle\0"
    "NotRightTriangleBar\0"
    "NotRightTriangleEqual\0"
    "NotSquareSubset\0"
    "NotSquareSubsetEqual\0"
    "NotSquareSuperset\0"
    "NotSquareSupersetEqual\0"
    "NotSubset\0"
    "NotSubsetEqual\0"
    "NotSucceeds\0"
    "NotSucceedsEqual\0"
    "NotSucceedsSlantEqual\0"
    "NotSucceedsTilde\0"
    "NotSuperset\0"
    "NotSupersetEqual\0"
    "NotTilde\0"
    "NotTildeEqual\0"
    "NotTildeFullEqual\0"
    "NotTildeTilde\0"
    "NotVerticalBar\0"
    "Nscr\0"
    "Ntilde\0"
    "Nu\0"
    "OElig\0"
    "Oacute\0"
    "Ocirc\0"
    "Ocy\0"
    "Odblac\0"
    "Ofr\0"
    "Ograve\0"
    "Omacr\0"
    "Omega\0"
    "Omicron\0"
    "Oopf\0"
    "OpenCurlyDoubleQuote\0"
    "OpenCurlyQuote\0"
    "Or\0"
    "Oscr\0"
    "Oslash\0"
    "Otilde\0"
    "Otimes\0"
    "Ouml\0"
    "OverBar\0"
    "OverBrace\0"
    "OverBracket\0"
    "OverParenthesis\0"
    "PartialD\0"
    "Pcy\0"
    "Pfr\0"
    "Phi\0"
    "Pi\0"
    "PlusMinus\0"
    "Poincareplane\0"
    "Popf\0"
    "Pr\0"
    "Precedes\0"
    "PrecedesEqual\0"
    "PrecedesSlantEqual\0"
    "PrecedesTilde\0"
    "Prime\0"
    "Product\0"
    "Proportion\0"
    "Proportional\0"
    "Pscr\0"
    "Psi\0"
    "QUOT\0"
    "Qfr\0"
    "Qopf\0"
    "Qscr\0"
    "RBarr\0"
    "REG\0"
    "Racute\0"
    "Rang\0"
    "Rarr\0"
    "Rarrtl\0"
    "Rcaron\0"
    "Rcedil\0"
    "Rcy\0"
    "Re\0"
    "ReverseElement\0"
    "ReverseEquilibrium\0"
    "ReverseUpEquilibrium\0"
    "Rfr\0"
    "Rho\0"
    "RightAngleBracket\0"
    "RightArrow\0"
    "RightArrowBar\0"
    "RightArrowLeftArrow\0"
    "RightCeiling\0"
    "RightDoubleBracket\0"
    "RightDownTeeVector\0"
    "RightDownVector\0"
    "RightDownVectorBar\0"
    "RightFloor\0"
    "RightTee\0"
    "RightTeeArrow\0"
    "RightTeeVector\0"
    "RightTriangle\0"
    "RightTriangleBar\0"
    "RightTriangleEqual\0"
    "RightUpDownVector\0"
    "RightUpTeeVector\0"
    "RightUpVector\0"
    "RightUpVectorBar\0"
    "RightVector\0"
    "RightVectorBar\0"
    "Rightarrow\0"
    "Ropf\0"
    "RoundImplies\0"
    "Rrightarrow\0"
    "Rscr\0"
    "Rsh\0"
    "RuleDelayed\0"
    "SHCHcy\0"
    "SHcy\0"
    "SOFTcy\0"
    "Sacute\0"
    "Sc\0"
    "Scaron\0"
    "Scedil\0"
    "Scirc\0"
    "Scy\0"
    "Sfr\0"
    "ShortDownArrow\0"
    "ShortLeftArrow\0"
    "ShortRightArrow\0"
    "ShortUpArrow\0"
    "Sigma\0"
    "SmallCircle\0"
    "Sopf\0"
    "Sqrt\0"
    "Square\0"
    "SquareIntersection\0"
    "SquareSubset\0"
    "SquareSubsetEqual\0"
    "SquareSuperset\0"
    "SquareSupersetEqual\0"
    "SquareUnion\0"
    "Sscr\0"
    "Star\0"
    "Sub\0"
    "Subset\0"
    "SubsetEqual\0"
    "Succeeds\0"
    "SucceedsEqual\0"
    "SucceedsSlantEqual\0"
    "SucceedsTilde\0"
    "SuchThat\0"
    "Sum\0"
    "Sup\0"
    "Superset\0"
    "SupersetEqual\0"
    "Supset\0"
    "THORN\0"
    "TRADE\0"
    "TSHcy\0"
    "TScy\0"
    "Tab\0"
    "Tau\0"
    "Tcaron\0"
    "Tcedil\0"
    "Tcy\0"
    "Tfr\0"
    "Therefore\0"
    "Theta\0"
    "ThickSpace\0"
    "ThinSpace\0"
    "Tilde\0"
    "TildeEqual\0"
    "TildeFullEqual\0"
    "TildeTilde\0"
    "Topf\0"
    "TripleDot\0"
    "Tscr\0"
    "Tstrok\0"
    "Uacute\0"
    "Uarr\0"
    "Uarrocir\0"
    "Ubrcy\0"
    "Ubreve\0"
    "Ucirc\0"
    "Ucy\0"
    "Udblac\0"
    "Ufr\0"
    "Ugrave\0"
    "Umacr\0"
    "UnderBar\0"
    "UnderBrace\0"
    "UnderBracket\0"
    "UnderParenthesis\0"
    "Union\0"
    "UnionPlus\0"
    "Uogon\0"
    "Uopf\0"
    "UpArrow\0"
    "UpArrowBar\0"
    "UpArrowDownArrow\0"
    "UpDownArrow\0"
    "UpEquilibrium\0"
    "UpTee\0"
    "UpTeeArrow\0"
    "Uparrow\0"
    "Updownarrow\0"
    "UpperLeftArrow\0"
    "UpperRightArrow\0"
    "Upsi\0"
    "Upsilon\0"
    "Uring\0"
    "Uscr\0"
    "Utilde\0"
    "Uuml\0"
    "VDash\0"
    "Vbar\0"
    "Vcy\0"
    "Vdash\0"
    "Vdashl\0"
    "Vee\0"
    "Verbar\0"
    "Vert\0"
    "VerticalBar\0"
    "VerticalLine\0"
    "VerticalSeparator\0"
    "VerticalTilde\0"
    "VeryThinSpace\0"
    "Vfr\0"
    "Vopf\0"
    "Vscr\0"
    "Vvdash\0"
    "Wcirc\0"
    "Wedge\0"
    "Wfr\0"
    "Wopf\0"
    "Wscr\0"
    "Xfr\0"
    "Xi\0"
    "Xopf\0"
    "Xscr\0"
    "YAcy\0"
    "YIcy\0"
    "YUcy\0"
    "Yacute\0"
    "Ycirc\0"
    "Ycy\0"
    "Yfr\0"
    "Yopf\0"
    "Yscr\0"
    "Yuml\0"
    "ZHcy\0"
    "Zacute\0"
    "Zcaron\0"
    "Zcy\0"
    "Zdot\0"
    "ZeroWidthSpace\0"
    "Zeta\0"
    "Zfr\0"
    "Zopf\0"
    "Zscr\0"
    "aacute\0"
    "abreve\0"
    "ac\0"
    "acE\0"
    "acd\0"
    "acirc\0"
    "acute\0"
    "acy\0"
    "aelig\0"
    "af\0"
    "afr\0"
    "agrave\0"
    "alefsym\0"
    "aleph\0"
    "alpha\0"
    "amacr\0"
    "amalg\0"
    "amp\0"
    "and\0"
    "andand\0"
    "andd\0"
    "andslope\0"
    "andv\0"
    "ang\0"
    "ange\0"
    "angle\0"
    "angmsd\0"
    "angmsdaa\0"
    "angmsdab\0"
    "angmsdac\0"
    "angmsdad\0"
    "angmsdae\0"
    "angmsdaf\0"
    "angmsdag\0"
    "angmsdah\0"
    "angrt\0"
    "angrtvb\0"
    "angrtvbd\0"
    "angsph\0"
    "angst\0"
    "angzarr\0"
    "aogon\0"
    "aopf\0"
    "ap\0"
    "apE\0"
    "apacir\0"
    "ape\0"
    "apid\0"
    "apos\0"
    "approx\0"
    "approxeq\0"
    "aring\0"
    "ascr\0"
    "ast\0"
    "asymp\0"
    "asympeq\0"
    "atilde\0"
    "auml\0"
    "awconint\0"
    "awint\0"
    "bNot\0"
    "backcong\0"
    "backepsilon\0"
    "backprime\0"
    "backsim\0"
    "backsimeq\0"
    "barvee\0"
    "barwed\0"
    "barwedge\0"
    "bbrk\0"
    "bbrktbrk\0"
    "bcong\0"
    "bcy\0"
    "bdquo\0"
    "becaus\0"
    "because\0"
    "bemptyv\0"
    "bepsi\0"
    "bernou\0"
    "beta\0"
    "beth\0"
    "between\0"
    "bfr\0"
    "bigcap\0"
    "bigcirc\0"
    "bigcup\0"
    "bigodot\0"
    "bigoplus\0"
    "bigotimes\0"
    "bigsqcup\0"
    "bigstar\0"
    "bigtriangledown\0"
    "bigtriangleup\0"
    "biguplus\0"
    "bigvee\0"
    "bigwedge\0"
    "bkarow\0"
    "blacklozenge\0"
    "blacksquare\0"
    "blacktriangle\0"
    "blacktriangledown\0"
    "blacktriangleleft\0"
    "blacktriangleright\0"
    "blank\0"
    "blk12\0"
    "blk14\0"
    "blk34\0"
    "block\0"
    "bne\0"
    "bnequiv\0"
    "bnot\0"
    "bopf\0"
    "bot\0"
    "bottom\0"
    "bowtie\0"
    "boxDL\0"
    "boxDR\0"
    "boxDl\0"
    "boxDr\0"
    "boxH\0"
    "boxHD\0"
    "boxHU\0"
    "boxHd\0"
    "boxHu\0"
    "boxUL\0"
    "boxUR\0"
    "boxUl\0"
    "boxUr\0"
    "boxV\0"
    "boxVH\0"
    "boxVL\0"
    "boxVR\0"
    "boxVh\0"
    "boxVl\0"
    "boxVr\0"
    "boxbox\0"
    "boxdL\0"
    "boxdR\0"
    "boxdl\0"
    "boxdr\0"
    "boxh\0"
    "boxhD\0"
    "boxhU\0"
    "boxhd\0"
    "boxhu\0"
    "boxminus\0"
    "boxplus\0"
    "boxtimes\0"
    "boxuL\0"
    "boxuR\0"
    "boxul\0"
    "boxur\0"
    "boxv\0"
    "boxvH\0"
    "boxvL\0"
    "boxvR\0"
    "boxvh\0"
    "boxvl\0"
    "boxvr\0"
    "bprime\0"
    "breve\0"
    "brvbar\0"
    "bscr\0"
    "bsemi\0"
    "bsim\0"
    "bsime\0"
    "bsol\0"
    "bsolb\0"
    "bsolhsub\0"
    "bull\0"
    "bullet\0"
    "bump\0"
    "bumpE\0"
    "bumpe\0"
    "bumpeq\0"
    "cacute\0"
    "cap\0"
    "capand\0"
    "capbrcup\0"
    "capcap\0"
    "capcup\0"
    "capdot\0"
    "caps\0"
    "caret\0"
    "caron\0"
    "ccaps\0"
    "ccaron\0"
    "ccedil\0"
    "ccirc\0"
    "ccups\0"
    "ccupssm\0"
    "cdot\0"
    "cedil\0"
    "cemptyv\0"
    "cent\0"
    "centerdot\0"
    "cfr\0"
    "chcy\0"
    "check\0"
    "checkmark\0"
    "chi\0"
    "cir\0"
    "cirE\0"
    "circ\0"
    "circeq\0"
    "circlearrowleft\0"
    "circlearrowright\0"
    "circledR\0"
    "circledS\0"
    "circledast\0"
    "circledcirc\0"
    "circleddash\0"
    "cire\0"
    "cirfnint\0"
    "cirmid\0"
    "cirscir\0"
    "clubs\0"
    "clubsuit\0"
    "colon\0"
    "colone\0"
    "coloneq\0"
    "comma\0"
    "commat\0"
    "comp\0"
    "compfn\0"
    "complement\0"
    "complexes\0"
    "cong\0"
    "congdot\0"
    "conint\0"
    "copf\0"
    "coprod\0"
    "copy\0"
    "copysr\0"
    "crarr\0"
    "cross\0"
    "cscr\0"
    "csub\0"
    "csube\0"
    "csup\0"
    "csupe\0"
    "ctdot\0"
    "cudarrl\0"
    "cudarrr\0"
    "cuepr\0"
    "cuesc\0"
    "cularr\0"
    "cularrp\0"
    "cup\0"
    "cupbrcap\0"
    "cupcap\0"
    "cupcup\0"
    "cupdot\0"
    "cupor\0"
    "cups\0"
    "curarr\0"
    "curarrm\0"
    "curlyeqprec\0"
    "curlyeqsucc\0"
    "curlyvee\0"
    "curlywedge\0"
    "curren\0"
    "curvearrowleft\0"
    "curvearrowright\0"
    "cuvee\0"
    "cuwed\0"
    "cwconint\0"
    "cwint\0"
    "cylcty\0"
    "dArr\0"
    "dHar\0"
    "dagger\0"
    "daleth\0"
    "darr\0"
    "dash\0"
    "dashv\0"
    "dbkarow\0"
    "dblac\0"
    "dcaron\0"
    "dcy\0"
    "dd\0"
    "ddagger\0"
    "ddarr\0"
    "ddotseq\0"
    "deg\0"
    "delta\0"
    "demptyv\0"
    "dfisht\0"
    "dfr\0"
    "dharl\0"
    "dharr\0"
    "diam\0"
    "diamond\0"
    "diamondsuit\0"
    "diams\0"
    "die\0"
    "digamma\0"
    "disin\0"
    "div\0"
    "divide\0"
    "divideontimes\0"
    "divonx\0"
    "djcy\0"
    "dlcorn\0"
    "dlcrop\0"
    "dollar\0"
    "dopf\0"
    "dot\0"
    "doteq\0"
    "doteqdot\0"
    "dotminus\0"
    "dotplus\0"
    "dotsquare\0"
    "doublebarwedge\0"
    "downarrow\0"
    "downdownarrows\0"
    "downharpoonleft\0"
    "downharpoonright\0"
    "drbkarow\0"
    "drcorn\0"
    "drcrop\0"
    "dscr\0"
    "dscy\0"
    "dsol\0"
    "dstrok\0"
    "dtdot\0"
    "dtri\0"
    "dtrif\0"
    "duarr\0"
    "duhar\0"
    "dwangle\0"
    "dzcy\0"
    "dzigrarr\0"
    "eDDot\0"
    "eDot\0"
    "eacute\0"
    "easter\0"
    "ecaron\0"
    "ecir\0"
    "ecirc\0"
    "ecolon\0"
    "ecy\0"
    "edot\0"
    "ee\0"
    "efDot\0"
    "efr\0"
    "eg\0"
    "egrave\0"
    "egs\0"
    "egsdot\0"
    "el\0"
    "elinters\0"
    "ell\0"
    "els\0"
    "elsdot\0"
    "emacr\0"
    "empty\0"
    "emptyset\0"
    "emptyv\0"
    "emsp\0"
    "emsp13\0"
    "emsp14\0"
    "eng\0"
    "ensp\0"
    "eogon\0"
    "eopf\0"
    "epar\0"
    "eparsl\0"
    "eplus\0"
    "epsi\0"
    "epsilon\0"
    "epsiv\0"
    "eqcirc\0"
    "eqcolon\0"
    "eqsim\0"
    "eqslantgtr\0"
    "eqslantless\0"
    "equals\0"
    "equest\0"
    "equiv\0"
    "equivDD\0"
    "eqvparsl\0"
    "erDot\0"
    "erarr\0"
    "escr\0"
    "esdot\0"
    "esim\0"
    "eta\0"
    "eth\0"
    "euml\0"
    "euro\0"
    "excl\0"
    "exist\0"
    "expectation\0"
    "exponentiale\0"
    "fallingdotseq\0"
    "fcy\0"
    "female\0"
    "ffilig\0"
    "fflig\0"
    "ffllig\0"
    "ffr\0"
    "filig\0"
    "fjlig\0"
    "flat\0"
    "fllig\0"
    "fltns\0"
    "fnof\0"
    "fopf\0"
    "forall\0"
    "fork\0"
    "forkv\0"
    "fpartint\0"
    "frac12\0"
    "frac13\0"
    "frac14\0"
    "frac15\0"
    "frac16\0"
    "frac18\0"
    "frac23\0"
    "frac25\0"
    "frac34\0"
    "frac35\0"
    "frac38\0"
    "frac45\0"
    "frac56\0"
    "frac58\0"
    "frac78\0"
    "frasl\0"
    "frown\0"
    "fscr\0"
    "gE\0"
    "gEl\0"
    "gacute\0"
    "gamma\0"
    "gammad\0"
    "gap\0"
    "gbreve\0"
    "gcirc\0"
    "gcy\0"
    "gdot\0"
    "ge\0"
    "gel\0"
    "geq\0"
    "geqq\0"
    "geqslant\0"
    "ges\0"
    "gescc\0"
    "gesdot\0"
    "gesdoto\0"
    "gesdotol\0"
    "gesl\0"
    "gesles\0"
    "gfr\0"
    "gg\0"
    "ggg\0"
    "gimel\0"
    "gjcy\0"
    "gl\0"
    "glE\0"
    "gla\0"
    "glj\0"
    "gnE\0"
    "gnap\0"
    "gnapprox\0"
    "gne\0"
    "gneq\0"
    "gneqq\0"
    "gnsim\0"
    "gopf\0"
    "grave\0"
    "gscr\0"
    "gsim\0"
    "gsime\0"
    "gsiml\0"
    "gt\0"
    "gtcc\0"
    "gtcir\0"
    "gtdot\0"
    "gtlPar\0"
    "gtquest\0"
    "gtrapprox\0"
    "gtrarr\0"
    "gtrdot\0"
    "gtreqless\0"
    "gtreqqless\0"
    "gtrless\0"
    "gtrsim\0"
    "gvertneqq\0"
    "gvnE\0"
    "hArr\0"
    "hairsp\0"
    "half\0"
    "hamilt\0"
    "hardcy\0"
    "harr\0"
    "harrcir\0"
    "harrw\0"
    "hbar\0"
    "hcirc\0"
    "hearts\0"
    "heartsuit\0"
    "hellip\0"
    "hercon\0"
    "hfr\0"
    "hksearow\0"
    "hkswarow\0"
    "hoarr\0"
    "homtht\0"
    "hookleftarrow\0"
    "hookrightarrow\0"
    "hopf\0"
    "horbar\0"
    "hscr\0"
    "hslash\0"
    "hstrok\0"
    "hybull\0"
    "hyphen\0"
    "iacute\0"
    "ic\0"
    "icirc\0"
    "icy\0"
    "iecy\0"
    "iexcl\0"
    "iff\0"
    "ifr\0"
    "igrave\0"
    "ii\0"
    "iiiint\0"
    "iiint\0"
    "iinfin\0"
    "iiota\0"
    "ijlig\0"
    "imacr\0"
    "image\0"
    "imagline\0"
    "imagpart\0"
    "imath\0"
    "imof\0"
    "imped\0"
    "in\0"
    "incare\0"
    "infin\0"
    "infintie\0"
    "inodot\0"
    "int\0"
    "intcal\0"
    "integers\0"
    "intercal\0"
    "intlarhk\0"
    "intprod\0"
    "iocy\0"
    "iogon\0"
    "iopf\0"
    "iota\0"
    "iprod\0"
    "iquest\0"
    "iscr\0"
    "isin\0"
    "isinE\0"
    "isindot\0"
    "isins\0"
    "isinsv\0"
    "isinv\0"
    "it\0"
    "itilde\0"
    "iukcy\0"
    "iuml\0"
    "jcirc\0"
    "jcy\0"
    "jfr\0"
    "jmath\0"
    "jopf\0"
    "jscr\0"
    "jsercy\0"
    "jukcy\0"
    "kappa\0"
    "kappav\0"
    "kcedil\0"
    "kcy\0"
    "kfr\0"
    "kgreen\0"
    "khcy\0"
    "kjcy\0"
    "kopf\0"
    "kscr\0"
    "lAarr\0"
    "lArr\0"
    "lAtail\0"
    "lBarr\0"
    "lE\0"
    "lEg\0"
    "lHar\0"
    "lacute\0"
    "laemptyv\0"
    "lagran\0"
    "lambda\0"
    "lang\0"
    "langd\0"
    "langle\0"
    "lap\0"
    "laquo\0"
    "larr\0"
    "larrb\0"
    "larrbfs\0"
    "larrfs\0"
    "larrhk\0"
    "larrlp\0"
    "larrpl\0"
    "larrsim\0"
    "larrtl\0"
    "lat\0"
    "latail\0"
    "late\0"
    "lates\0"
    "lbarr\0"
    "lbbrk\0"
    "lbrace\0"
    "lbrack\0"
    "lbrke\0"
    "lbrksld\0"
    "lbrkslu\0"
    "lcaron\0"
    "lcedil\0"
    "lceil\0"
    "lcub\0"
    "lcy\0"
    "ldca\0"
    "ldquo\0"
    "ldquor\0"
    "ldrdhar\0"
    "ldrushar\0"
    "ldsh\0"
    "le\0"
    "leftarrow\0"
    "leftarrowtail\0"
    "leftharpoondown\0"
    "leftharpoonup\0"
    "leftleftarrows\0"
    "leftrightarrow\0"
    "leftrightarrows\0"
    "leftrightharpoons\0"
    "leftrightsquigarrow\0"
    "leftthreetimes\0"
    "leg\0"
    "leq\0"
    "leqq\0"
    "leqslant\0"
    "les\0"
    "lescc\0"
    "lesdot\0"
    "lesdoto\0"
    "lesdotor\0"
    "lesg\0"
    "lesges\0"
    "lessapprox\0"
    "lessdot\0"
    "lesseqgtr\0"
    "lesseqqgtr\0"
    "lessgtr\0"
    "lesssim\0"
    "lfisht\0"
    "lfloor\0"
    "lfr\0"
    "lg\0"
    "lgE\0"
    "lhard\0"
    "lharu\0"
    "lharul\0"
    "lhblk\0"
    "ljcy\0"
    "ll\0"
    "llarr\0"
    "llcorner\0"
    "llhard\0"
    "lltri\0"
    "lmidot\0"
    "lmoust\0"
    "lmoustache\0"
    "lnE\0"
    "lnap\0"
    "lnapprox\0"
    "lne\0"
    "lneq\0"
    "lneqq\0"
    "lnsim\0"
    "loang\0"
    "loarr\0"
    "lobrk\0"
    "longleftarrow\0"
    "longleftrightarrow\0"
    "longmapsto\0"
    "longrightarrow\0"
    "looparrowleft\0"
    "looparrowright\0"
    "lopar\0"
    "lopf\0"
    "loplus\0"
    "lotimes\0"
    "lowast\0"
    "lowbar\0"
    "loz\0"
    "lozenge\0"
    "lozf\0"
    "lpar\0"
    "lparlt\0"
    "lrarr\0"
    "lrcorner\0"
    "lrhar\0"
    "lrhard\0"
    "lrm\0"
    "lrtri\0"
    "lsaquo\0"
    "lscr\0"
    "lsh\0"
    "lsim\0"
    "lsime\0"
    "lsimg\0"
    "lsqb\0"
    "lsquo\0"
    "lsquor\0"
    "lstrok\0"
    "lt\0"
    "ltcc\0"
    "ltcir\0"
    "ltdot\0"
    "lthree\0"
    "ltimes\0"
    "ltlarr\0"
    "ltquest\0"
    "ltrPar\0"
    "ltri\0"
    "ltrie\0"
    "ltrif\0"
    "lurdshar\0"
    "luruhar\0"
    "lvertneqq\0"
    "lvnE\0"
    "mDDot\0"
    "macr\0"
    "male\0"
    "malt\0"
    "maltese\0"
    "map\0"
    "mapsto\0"
    "mapstodown\0"
    "mapstoleft\0"
    "mapstoup\0"
    "marker\0"
    "mcomma\0"
    "mcy\0"
    "mdash\0"
    "measuredangle\0"
    "mfr\0"
    "mho\0"
    "micro\0"
    "mid\0"
    "midast\0"
    "midcir\0"
    "middot\0"
    "minus\0"
    "minusb\0"
    "minusd\0"
    "minusdu\0"
    "mlcp\0"
    "mldr\0"
    "mnplus\0"
    "models\0"
    "mopf\0"
    "mp\0"
    "mscr\0"
    "mstpos\0"
    "mu\0"
    "multimap\0"
    "mumap\0"
    "nGg\0"
    "nGt\0"
    "nGtv\0"
    "nLeftarrow\0"
    "nLeftrightarrow\0"
    "nLl\0"
    "nLt\0"
    "nLtv\0"
    "nRightarrow\0"
    "nVDash\0"
    "nVdash\0"
    "nabla\0"
    "nacute\0"
    "nang\0"
    "nap\0"
    "napE\0"
    "napid\0"
    "napos\0"
    "napprox\0"
    "natur\0"
    "natural\0"
    "naturals\0"
    "nbsp\0"
    "nbump\0"
    "nbumpe\0"
    "ncap\0"
    "ncaron\0"
    "ncedil\0"
    "ncong\0"
    "ncongdot\0"
    "ncup\0"
    "ncy\0"
    "ndash\0"
    "ne\0"
    "neArr\0"
    "nearhk\0"
    "nearr\0"
    "nearrow\0"
    "nedot\0"
    "nequiv\0"
    "nesear\0"
    "nesim\0"
    "nexist\0"
    "nexists\0"
    "nfr\0"
    "ngE\0"
    "nge\0"
    "ngeq\0"
    "ngeqq\0"
    "ngeqslant\0"
    "nges\0"
    "ngsim\0"
    "ngt\0"
    "ngtr\0"
    "nhArr\0"
    "nharr\0"
    "nhpar\0"
    "ni\0"
    "nis\0"
    "nisd\0"
    "niv\0"
    "njcy\0"
    "nlArr\0"
    "nlE\0"
    "nlarr\0"
    "nldr\0"
    "nle\0"
    "nleftarrow\0"
    "nleftrightarrow\0"
    "nleq\0"
    "nleqq\0"
    "nleqslant\0"
    "nles\0"
    "nless\0"
    "nlsim\0"
    "nlt\0"
    "nltri\0"
    "nltrie\0"
    "nmid\0"
    "nopf\0"
    "not\0"
    "notin\0"
    "notinE\0"
    "notindot\0"
    "notinva\0"
    "notinvb\0"
    "notinvc\0"
    "notni\0"
    "notniva\0"
    "notnivb\0"
    "notnivc\0"
    "npar\0"
    "nparallel\0"
    "nparsl\0"
    "npart\0"
    "npolint\0"
    "npr\0"
    "nprcue\0"
    "npre\0"
    "nprec\0"
    "npreceq\0"
    "nrArr\0"
    "nrarr\0"
    "nrarrc\0"
    "nrarrw\0"
    "nrightarrow\0"
    "nrtri\0"
    "nrtrie\0"
    "nsc\0"
    "nsccue\0"
    "nsce\0"
    "nscr\0"
    "nshortmid\0"
    "nshortparallel\0"
    "nsim\0"
    "nsime\0"
    "nsimeq\0"
    "nsmid\0"
    "nspar\0"
    "nsqsube\0"
    "nsqsupe\0"
    "nsub\0"
    "nsubE\0"
    "nsube\0"
    "nsubset\0"
    "nsubseteq\0"
    "nsubseteqq\0"
    "nsucc\0"
    "nsucceq\0"
    "nsup\0"
    "nsupE\0"
    "nsupe\0"
    "nsupset\0"
    "nsupseteq\0"
    "nsupseteqq\0"
    "ntgl\0"
    "ntilde\0"
    "ntlg\0"
    "ntriangleleft\0"
    "ntrianglelefteq\0"
    "ntriangleright\0"
    "ntrianglerighteq\0"
    "nu\0"
    "num\0"
    "numero\0"
    "numsp\0"
    "nvDash\0"
    "nvHarr\0"
    "nvap\0"
    "nvdash\0"
    "nvge\0"
    "nvgt\0"
    "nvinfin\0"
    "nvlArr\0"
    "nvle\0"
    "nvlt\0"
    "nvltrie\0"
    "nvrArr\0"
    "nvrtrie\0"
    "nvsim\0"
    "nwArr\0"
    "nwarhk\0"
    "nwarr\0"
    "nwarrow\0"
    "nwnear\0"
    "oS\0"
    "oacute\0"
    "oast\0"
    "ocir\0"
    "ocirc\0"
    "ocy\0"
    "odash\0"
    "odblac\0"
    "odiv\0"
    "odot\0"
    "odsold\0"
    "oelig\0"
    "ofcir\0"
    "ofr\0"
    "ogon\0"
    "ograve\0"
    "ogt\0"
    "ohbar\0"
    "ohm\0"
    "oint\0"
    "olarr\0"
    "olcir\0"
    "olcross\0"
    "oline\0"
    "olt\0"
    "omacr\0"
    "omega\0"
    "omicron\0"
    "omid\0"
    "ominus\0"
    "oopf\0"
    "opar\0"
    "operp\0"
    "oplus\0"
    "or\0"
    "orarr\0"
    "ord\0"
    "order\0"
    "orderof\0"
    "ordf\0"
    "ordm\0"
    "origof\0"
    "oror\0"
    "orslope\0"
    "orv\0"
    "oscr\0"
    "oslash\0"
    "osol\0"
    "otilde\0"
    "otimes\0"
    "otimesas\0"
    "ouml\0"
    "ovbar\0"
    "par\0"
    "para\0"
    "parallel\0"
    "parsim\0"
    "parsl\0"
    "part\0"
    "pcy\0"
    "percnt\0"
    "period\0"
    "permil\0"
    "perp\0"
    "pertenk\0"
    "pfr\0"
    "phi\0"
    "phiv\0"
    "phmmat\0"
    "phone\0"
    "pi\0"
    "pitchfork\0"
    "piv\0"
    "planck\0"
    "planckh\0"
    "plankv\0"
    "plus\0"
    "plusacir\0"
    "plusb\0"
    "pluscir\0"
    "plusdo\0"
    "plusdu\0"
    "pluse\0"
    "plusmn\0"
    "plussim\0"
    "plustwo\0"
    "pm\0"
    "pointint\0"
    "popf\0"
    "pound\0"
    "pr\0"
    "prE\0"
    "prap\0"
    "prcue\0"
    "pre\0"
    "prec\0"
    "precapprox\0"
    "preccurlyeq\0"
    "preceq\0"
    "precnapprox\0"
    "precneqq\0"
    "precnsim\0"
    "precsim\0"
    "prime\0"
    "primes\0"
    "prnE\0"
    "prnap\0"
    "prnsim\0"
    "prod\0"
    "profalar\0"
    "profline\0"
    "profsurf\0"
    "prop\0"
    "propto\0"
    "prsim\0"
    "prurel\0"
    "pscr\0"
    "psi\0"
    "puncsp\0"
    "qfr\0"
    "qint\0"
    "qopf\0"
    "qprime\0"
    "qscr\0"
    "quaternions\0"
    "quatint\0"
    "quest\0"
    "questeq\0"
    "quot\0"
    "rAarr\0"
    "rArr\0"
    "rAtail\0"
    "rBarr\0"
    "rHar\0"
    "race\0"
    "racute\0"
    "radic\0"
    "raemptyv\0"
    "rang\0"
    "rangd\0"
    "range\0"
    "rangle\0"
    "raquo\0"
    "rarr\0"
    "rarrap\0"
    "rarrb\0"
    "rarrbfs\0"
    "rarrc\0"
    "rarrfs\0"
    "rarrhk\0"
    "rarrlp\0"
    "rarrpl\0"
    "rarrsim\0"
    "rarrtl\0"
    "rarrw\0"
    "ratail\0"
    "ratio\0"
    "rationals\0"
    "rbarr\0"
    "rbbrk\0"
    "rbrace\0"
    "rbrack\0"
    "rbrke\0"
    "rbrksld\0"
    "rbrkslu\0"
    "rcaron\0"
    "rcedil\0"
    "rceil\0"
    "rcub\0"
    "rcy\0"
    "rdca\0"
    "rdldhar\0"
    "rdquo\0"
    "rdquor\0"
    "rdsh\0"
    "real\0"
    "realine\0"
    "realpart\0"
    "reals\0"
    "rect\0"
    "reg\0"
    "rfisht\0"
    "rfloor\0"
    "rfr\0"
    "rhard\0"
    "rharu\0"
    "rharul\0"
    "rho\0"
    "rhov\0"
    "rightarrow\0"
    "rightarrowtail\0"
    "rightharpoondown\0"
    "rightharpoonup\0"
    "rightleftarrows\0"
    "rightleftharpoons\0"
    "rightrightarrows\0"
    "rightsquigarrow\0"
    "rightthreetimes\0"
    "ring\0"
    "risingdotseq\0"
    "rlarr\0"
    "rlhar\0"
    "rlm\0"
    "rmoust\0"
    "rmoustache\0"
    "rnmid\0"
    "roang\0"
    "roarr\0"
    "robrk\0"
    "ropar\0"
    "ropf\0"
    "roplus\0"
    "rotimes\0"
    "rpar\0"
    "rpargt\0"
    "rppolint\0"
    "rrarr\0"
    "rsaquo\0"
    "rscr\0"
    "rsh\0"
    "rsqb\0"
    "rsquo\0"
    "rsquor\0"
    "rthree\0"
    "rtimes\0"
    "rtri\0"
    "rtrie\0"
    "rtrif\0"
    "rtriltri\0"
    "ruluhar\0"
    "rx\0"
    "sacute\0"
    "sbquo\0"
    "sc\0"
    "scE\0"
    "scap\0"
    "scaron\0"
    "sccue\0"
    "sce\0"
    "scedil\0"
    "scirc\0"
    "scnE\0"
    "scnap\0"
    "scnsim\0"
    "scpolint\0"
    "scsim\0"
    "scy\0"
    "sdot\0"
    "sdotb\0"
    "sdote\0"
    "seArr\0"
    "searhk\0"
    "searr\0"
    "searrow\0"
    "sect\0"
    "semi\0"
    "seswar\0"
    "setminus\0"
    "setmn\0"
    "sext\0"
    "sfr\0"
    "sfrown\0"
    "sharp\0"
    "shchcy\0"
    "shcy\0"
    "shortmid\0"
    "shortparallel\0"
    "shy\0"
    "sigma\0"
    "sigmaf\0"
    "sigmav\0"
    "sim\0"
    "simdot\0"
    "sime\0"
    "simeq\0"
    "simg\0"
    "simgE\0"
    "siml\0"
    "simlE\0"
    "simne\0"
    "simplus\0"
    "simrarr\0"
    "slarr\0"
    "smallsetminus\0"
    "smashp\0"
    "smeparsl\0"
    "smid\0"
    "smile\0"
    "smt\0"
    "smte\0"
    "smtes\0"
    "softcy\0"
    "sol\0"
    "solb\0"
    "solbar\0"
    "sopf\0"
    "spades\0"
    "spadesuit\0"
    "spar\0"
    "sqcap\0"
    "sqcaps\0"
    "sqcup\0"
    "sqcups\0"
    "sqsub\0"
    "sqsube\0"
    "sqsubset\0"
    "sqsubseteq\0"
    "sqsup\0"
    "sqsupe\0"
    "sqsupset\0"
    "sqsupseteq\0"
    "squ\0"
    "square\0"
    "squarf\0"
    "squf\0"
    "srarr\0"
    "sscr\0"
    "ssetmn\0"
    "ssmile\0"
    "sstarf\0"
    "star\0"
    "starf\0"
    "straightepsilon\0"
    "straightphi\0"
    "strns\0"
    "sub\0"
    "subE\0"
    "subdot\0"
    "sube\0"
    "subedot\0"
    "submult\0"
    "subnE\0"
    "subne\0"
    "subplus\0"
    "subrarr\0"
    "subset\0"
    "subseteq\0"
    "subseteqq\0"
    "subsetneq\0"
    "subsetneqq\0"
    "subsim\0"
    "subsub\0"
    "subsup\0"
    "succ\0"
    "succapprox\0"
    "succcurlyeq\0"
    "succeq\0"
    "succnapprox\0"
    "succneqq\0"
    "succnsim\0"
    "succsim\0"
    "sum\0"
    "sung\0"
    "sup\0"
    "sup1\0"
    "sup2\0"
    "sup3\0"
    "supE\0"
    "supdot\0"
    "supdsub\0"
    "supe\0"
    "supedot\0"
    "suphsol\0"
    "suphsub\0"
    "suplarr\0"
    "supmult\0"
    "supnE\0"
    "supne\0"
    "supplus\0"
    "supset\0"
    "supseteq\0"
    "supseteqq\0"
    "supsetneq\0"
    "supsetneqq\0"
    "supsim\0"
    "supsub\0"
    "supsup\0"
    "swArr\0"
    "swarhk\0"
    "swarr\0"
    "swarrow\0"
    "swnwar\0"
    "szlig\0"
    "target\0"
    "tau\0"
    "tbrk\0"
    "tcaron\0"
    "tcedil\0"
    "tcy\0"
    "tdot\0"
    "telrec\0"
    "tfr\0"
    "there4\0"
    "therefore\0"
    "theta\0"
    "thetasym\0"
    "thetav\0"
    "thickapprox\0"
    "thicksim\0"
    "thinsp\0"
    "thkap\0"
    "thksim\0"
    "thorn\0"
    "tilde\0"
    "times\0"
    "timesb\0"
    "timesbar\0"
    "timesd\0"
    "tint\0"
    "toea\0"
    "top\0"
    "topbot\0"
    "topcir\0"
    "topf\0"
    "topfork\0"
    "tosa\0"
    "tprime\0"
    "trade\0"
    "triangle\0"
    "triangledown\0"
    "triangleleft\0"
    "trianglelefteq\0"
    "triangleq\0"
    "triangleright\0"
    "trianglerighteq\0"
    "tridot\0"
    "trie\0"
    "triminus\0"
    "triplus\0"
    "trisb\0"
    "tritime\0"
    "trpezium\0"
    "tscr\0"
    "tscy\0"
    "tshcy\0"
    "tstrok\0"
    "twixt\0"
    "twoheadleftarrow\0"
    "twoheadrightarrow\0"
    "uArr\0"
    "uHar\0"
    "uacute\0"
    "uarr\0"
    "ubrcy\0"
    "ubreve\0"
    "ucirc\0"
    "ucy\0"
    "udarr\0"
    "udblac\0"
    "udhar\0"
    "ufisht\0"
    "ufr\0"
    "ugrave\0"
    "uharl\0"
    "uharr\0"
    "uhblk\0"
    "ulcorn\0"
    "ulcorner\0"
    "ulcrop\0"
    "ultri\0"
    "umacr\0"
    "uml\0"
    "uogon\0"
    "uopf\0"
    "uparrow\0"
    "updownarrow\0"
    "upharpoonleft\0"
    "upharpoonright\0"
    "uplus\0"
    "upsi\0"
    "upsih\0"
    "upsilon\0"
    "upuparrows\0"
    "urcorn\0"
    "urcorner\0"
    "urcrop\0"
    "uring\0"
    "urtri\0"
    "uscr\0"
    "utdot\0"
    "utilde\0"
    "utri\0"
    "utrif\0"
    "uuarr\0"
    "uuml\0"
    "uwangle\0"
    "vArr\0"
    "vBar\0"
    "vBarv\0"
    "vDash\0"
    "vangrt\0"
    "varepsilon\0"
    "varkappa\0"
    "varnothing\0"
    "varphi\0"
    "varpi\0"
    "varpropto\0"
    "varr\0"
    "varrho\0"
    "varsigma\0"
    "varsubsetneq\0"
    "varsubsetneqq\0"
    "varsupsetneq\0"
    "varsupsetneqq\0"
    "vartheta\0"
    "vartriangleleft\0"
    "vartriangleright\0"
    "vcy\0"
    "vdash\0"
    "vee\0"
    "veebar\0"
    "veeeq\0"
    "vellip\0"
    "verbar\0"
    "vert\0"
    "vfr\0"
    "vltri\0"
    "vnsub\0"
    "vnsup\0"
    "vopf\0"
    "vprop\0"
    "vrtri\0"
    "vscr\0"
    "vsubnE\0"
    "vsubne\0"
    "vsupnE\0"
    "vsupne\0"
    "vzigzag\0"
    "wcirc\0"
    "wedbar\0"
    "wedge\0"
    "wedgeq\0"
    "weierp\0"
    "wfr\0"
    "wopf\0"
    "wp\0"
    "wr\0"
    "wreath\0"
    "wscr\0"
    "xcap\0"
    "xcirc\0"
    "xcup\0"
    "xdtri\0"
    "xfr\0"
    "xhArr\0"
    "xharr\0"
    "xi\0"
    "xlArr\0"
    "xlarr\0"
    "xmap\0"
    "xnis\0"
    "xodot\0"
    "xopf\0"
    "xoplus\0"
    "xotime\0"
    "xrArr\0"
    "xrarr\0"
    "xscr\0"
    "xsqcup\0"
    "xuplus\0"
    "xutri\0"
    "xvee\0"
    "xwedge\0"
    "yacute\0"
    "yacy\0"
    "ycirc\0"
    "ycy\0"
    "yen\0"
    "yfr\0"
    "yicy\0"
    "yopf\0"
    "yscr\0"
    "yucy\0"
    "yuml\0"
    "zacute\0"
    "zcaron\0"
    "zcy\0"
    "zdot\0"
    "zeetrf\0"
    "zeta\0"
    "zfr\0"
    "zhcy\0"
    "zigrarr\0"
    "zopf\0"
    "zscr\0"
    "zwj\0"
    "zwnj\0";

// Their values, in the same order.
const char kCharacterReferenceValues[] =
    "Æ\0"
    "&\0"
    "Á\0"
    "Ă\0"
    "Â\0"
    "А\0"
    "𝔄\0"
    "À\0"
    "Α\0"
    "Ā\0"
    "⩓\0"
    "Ą\0"
    "𝔸\0"
    "⁡\0"
    "Å\0"
    "𝒜\0"
    "≔\0"
    "Ã\0"
    "Ä\0"
    "∖\0"
    "⫧\0"
    "⌆\0"
    "Б\0"
    "∵\0"
    "ℬ\0"
    "Β\0"
    "𝔅\0"
    "𝔹\0"
    "˘\0"
    "ℬ\0"
    "≎\0"
    "Ч\0"
    "©\0"
    "Ć\0"
    "⋒\0"
    "ⅅ\0"
    "ℭ\0"
    "Č\0"
    "Ç\0"
    "Ĉ\0"
    "∰\0"
    "Ċ\0"
    "¸\0"
    "·\0"
    "ℭ\0"
    "Χ\0"
    "⊙\0"
    "⊖\0"
    "⊕\0"
    "⊗\0"
    "∲\0"
    "”\0"
    "’\0"
    "∷\0"
    "⩴\0"
    "≡\0"
    "∯\0"
    "∮\0"
    "ℂ\0"
    "∐\0"
    "∳\0"
    "⨯\0"
    "𝒞\0"
    "⋓\0"
    "≍\0"
    "ⅅ\0"
    "⤑\0"
    "Ђ\0"
    "Ѕ\0"
    "Џ\0"
    "‡\0"
    "↡\0"
    "⫤\0"
    "Ď\0"
    "Д\0"
    "∇\0"
    "Δ\0"
    "𝔇\0"
    "´\0"
    "˙\0"
    "˝\0"
    "`\0"
    "˜\0"
    "⋄\0"
    "ⅆ\0"
    "𝔻\0"
    "¨\0"
    "⃜\0"
    "≐\0"
    "∯\0"
    "¨\0"
    "⇓\0"
    "⇐\0"
    "⇔\0"
    "⫤\0"
    "⟸\0"
    "⟺\0"
    "⟹\0"
    "⇒\0"
    "⊨\0"
    "⇑\0"
    "⇕\0"
    "∥\0"
    "↓\0"
    "⤓\0"
    "⇵\0"
    "̑\0"
    "⥐\0"
    "⥞\0"
    "↽\0"
    "⥖\0"
    "⥟\0"
    "⇁\0"
    "⥗\0"
    "⊤\0"
    "↧\0"
    "⇓\0"
    "𝒟\0"
    "Đ\0"
    "Ŋ\0"
    "Ð\0"
    "É\0"
    "Ě\0"
    "Ê\0"
    "Э\0"
    "Ė\0"
    "𝔈\0"
    "È\0"
    "∈\0"
    "Ē\0"
    "◻\0"
    "▫\0"
    "Ę\0"
    "𝔼\0"
    "Ε\0"
    "⩵\0"
    "≂\0"
    "⇌\0"
    "ℰ\0"
    "⩳\0"
    "Η\0"
    "Ë\0"
    "∃\0"
    "ⅇ\0"
    "Ф\0"
    "𝔉\0"
    "◼\0"
    "▪\0"
    "𝔽\0"
    "∀\0"
    "ℱ\0"
    "ℱ\0"
    "Ѓ\0"
    ">\0"
    "Γ\0"
    "Ϝ\0"
    "Ğ\0"
    "Ģ\0"
    "Ĝ\0"
    "Г\0"
    "Ġ\0"
    "𝔊\0"
    "⋙\0"
    "𝔾\0"
    "≥\0"
    "⋛\0"
    "≧\0"
    "⪢\0"
    "≷\0"
    "⩾\0"
    "≳\0"
    "𝒢\0"
    "≫\0"
    "Ъ\0"
    "ˇ\0"
    "^\0"
    "Ĥ\0"
    "ℌ\0"
    "ℋ\0"
    "ℍ\0"
    "─\0"
    "ℋ\0"
    "Ħ\0"
    "≎\0"
    "≏\0"
    "Е\0"
    "Ĳ\0"
    "Ё\0"
    "Í\0"
    "Î\0"
    "И\0"
    "İ\0"
    "ℑ\0"
    "Ì\0"
    "ℑ\0"
    "Ī\0"
    "ⅈ\0"
    "⇒\0"
    "∬\0"
    "∫\0"
    "⋂\0"
    "⁣\0"
    "⁢\0"
    "Į\0"
    "𝕀\0"
    "Ι\0"
    "ℐ\0"
    "Ĩ\0"
    "І\0"
    "Ï\0"
    "Ĵ\0"
    "Й\0"
    "𝔍\0"
    "𝕁\0"
    "𝒥\0"
    "Ј\0"
    "Є\0"
    "Х\0"
    "Ќ\0"
    "Κ\0"
    "Ķ\0"
    "К\0"
    "𝔎\0"
    "𝕂\0"
    "𝒦\0"
    "Љ\0"
    "<\0"
    "Ĺ\0"
    "Λ\0"
    "⟪\0"
    "ℒ\0"
    "↞\0"
    "Ľ\0"
    "Ļ\0"
    "Л\0"
    "⟨\0"
    "←\0"
    "⇤\0"
    "⇆\0"
    "⌈\0"
    "⟦\0"
    "⥡\0"
    "⇃\0"
    "⥙\0"
    "⌊\0"
    "↔\0"
    "⥎\0"
    "⊣\0"
    "↤\0"
    "⥚\0"
    "⊲\0"
    "⧏\0"
    "⊴\0"
    "⥑\0"
    "⥠\0"
    "↿\0"
    "⥘\0"
    "↼\0"
    "⥒\0"
    "⇐\0"
    "⇔\0"
    "⋚\0"
    "≦\0"
    "≶\0"
    "⪡\0"
    "⩽\0"
    "≲\0"
    "𝔏\0"
    "⋘\0"
    "⇚\0"
    "Ŀ\0"
    "⟵\0"
    "⟷\0"
    "⟶\0"
    "⟸\0"
    "⟺\0"
    "⟹\0"
    "𝕃\0"
    "↙\0"
    "↘\0"
    "ℒ\0"
    "↰\0"
    "Ł\0"
    "≪\0"
    "⤅\0"
    "М\0"
    " \0"
    "ℳ\0"
    "𝔐\0"
    "∓\0"
    "𝕄\0"
    "ℳ\0"
    "Μ\0"
    "Њ\0"
    "Ń\0"
    "Ň\0"
    "Ņ\0"
    "Н\0"
    "​\0"
    "​\0"
    "​\0"
    "​\0"
    "≫\0"
    "≪\0"
    "\n\0"
    "𝔑\0"
    "⁠\0"
    " \0"
    "ℕ\0"
    "⫬\0"
    "≢\0"
    "≭\0"
    "∦\0"
    "∉\0"
    "≠\0"
    "≂̸\0"
    "∄\0"
    "≯\0"
    "≱\0"
    "≧̸\0"
    "≫̸\0"
    "≹\0"
    "⩾̸\0"
    "≵\0"
    "≎̸\0"
    "≏̸\0"
    "⋪\0"
    "⧏̸\0"
    "⋬\0"
    "≮\0"
    "≰\0"
    "≸\0"
    "≪̸\0"
    "⩽̸\0"
    "≴\0"
    "⪢̸\0"
    "⪡̸\0"
    "⊀\0"
    "⪯̸\0"
    "⋠\0"
    "∌\0"
    "⋫\0"
    "⧐̸\0"
    "⋭\0"
    "⊏̸\0"
    "⋢\0"
    "⊐̸\0"
    "⋣\0"
    "⊂⃒\0"
    "⊈\0"
    "⊁\0"
    "⪰̸\0"
    "⋡\0"
    "≿̸\0"
    "⊃⃒\0"
    "⊉\0"
    "≁\0"
    "≄\0"
    "≇\0"
    "≉\0"
    "∤\0"
    "𝒩\0"
    "Ñ\0"
    "Ν\0"
    "Œ\0"
    "Ó\0"
    "Ô\0"
    "О\0"
    "Ő\0"
    "𝔒\0"
    "Ò\0"
    "Ō\0"
    "Ω\0"
    "Ο\0"
    "𝕆\0"
    "“\0"
    "‘\0"
    "⩔\0"
    "𝒪\0"
    "Ø\0"
    "Õ\0"
    "⨷\0"
    "Ö\0"
    "‾\0"
    "⏞\0"
    "⎴\0"
    "⏜\0"
    "∂\0"
    "П\0"
    "𝔓\0"
    "Φ\0"
    "Π\0"
    "±\0"
    "ℌ\0"
    "ℙ\0"
    "⪻\0"
    "≺\0"
    "⪯\0"
    "≼\0"
    "≾\0"
    "″\0"
    "∏\0"
    "∷\0"
    "∝\0"
    "𝒫\0"
    "Ψ\0"
    "\"\0"
    "𝔔\0"
    "ℚ\0"
    "𝒬\0"
    "⤐\0"
    "®\0"
    "Ŕ\0"
    "⟫\0"
    "↠\0"
    "⤖\0"
    "Ř\0"
    "Ŗ\0"
    "Р\0"
    "ℜ\0"
    "∋\0"
    "⇋\0"
    "⥯\0"
    "ℜ\0"
    "Ρ\0"
    "⟩\0"
    "→\0"
    "⇥\0"
    "⇄\0"
    "⌉\0"
    "⟧\0"
    "⥝\0"
    "⇂\0"
    "⥕\0"
    "⌋\0"
    "⊢\0"
    "↦\0"
    "⥛\0"
    "⊳\0"
    "⧐\0"
    "⊵\0"
    "⥏\0"
    "⥜\0"
    "↾\0"
    "⥔\0"
    "⇀\0"
    "⥓\0"
    "⇒\0"
    "ℝ\0"
    "⥰\0"
    "⇛\0"
    "ℛ\0"
    "↱\0"
    "⧴\0"
    "Щ\0"
    "Ш\0"
    "Ь\0"
    "Ś\0"
    "⪼\0"
    "Š\0"
    "Ş\0"
    "Ŝ\0"
    "С\0"
    "𝔖\0"
    "↓\0"
    "←\0"
    "→\0"
    "↑\0"
    "Σ\0"
    "∘\0"
    "𝕊\0"
    "√\0"
    "□\0"
    "⊓\0"
    "⊏\0"
    "⊑\0"
    "⊐\0"
    "⊒\0"
    "⊔\0"
    "𝒮\0"
    "⋆\0"
    "⋐\0"
    "⋐\0"
    "⊆\0"
    "≻\0"
    "⪰\0"
    "≽\0"
    "≿\0"
    "∋\0"
    "∑\0"
    "⋑\0"
    "⊃\0"
    "⊇\0"
    "⋑\0"
    "Þ\0"
    "™\0"
    "Ћ\0"
    "Ц\0"
    "\t\0"
    "Τ\0"
    "Ť\0"
    "Ţ\0"
    "Т\0"
    "𝔗\0"
    "∴\0"
    "Θ\0"
    "  \0"
    " \0"
    "∼\0"
    "≃\0"
    "≅\0"
    "≈\0"
    "𝕋\0"
    "⃛\0"
    "𝒯\0"
    "Ŧ\0"
    "Ú\0"
    "↟\0"
    "⥉\0"
    "Ў\0"
    "Ŭ\0"
    "Û\0"
    "У\0"
    "Ű\0"
    "𝔘\0"
    "Ù\0"
    "Ū\0"
    "_\0"
    "⏟\0"
    "⎵\0"
    "⏝\0"
    "⋃\0"
    "⊎\0"
    "Ų\0"
    "𝕌\0"
    "↑\0"
    "⤒\0"
    "⇅\0"
    "↕\0"
    "⥮\0"
    "⊥\0"
    "↥\0"
    "⇑\0"
    "⇕\0"
    "↖\0"
    "↗\0"
    "ϒ\0"
    "Υ\0"
    "Ů\0"
    "𝒰\0"
    "Ũ\0"
    "Ü\0"
    "⊫\0"
    "⫫\0"
    "В\0"
    "⊩\0"
    "⫦\0"
    "⋁\0"
    "‖\0"
    "‖\0"
    "∣\0"
    "|\0"
    "❘\0"
    "≀\0"
    " \0"
    "𝔙\0"
    "𝕍\0"
    "𝒱\0"
    "⊪\0"
    "Ŵ\0"
    "⋀\0"
    "𝔚\0"
    "𝕎\0"
    "𝒲\0"
    "𝔛\0"
    "Ξ\0"
    "𝕏\0"
    "𝒳\0"
    "Я\0"
    "Ї\0"
    "Ю\0"
    "Ý\0"
    "Ŷ\0"
    "Ы\0"
    "𝔜\0"
    "𝕐\0"
    "𝒴\0"
    "Ÿ\0"
    "Ж\0"
    "Ź\0"
    "Ž\0"
    "З\0"
    "Ż\0"
    "​\0"
    "Ζ\0"
    "ℨ\0"
    "ℤ\0"
    "𝒵\0"
    "á\0"
    "ă\0"
    "∾\0"
    "∾̳\0"
    "∿\0"
    "â\0"
    "´\0"
    "а\0"
    "æ\0"
    "⁡\0"
    "𝔞\0"
    "à\0"
    "ℵ\0"
    "ℵ\0"
    "α\0"
    "ā\0"
    "⨿\0"
    "&\0"
    "∧\0"
    "⩕\0"
    "⩜\0"
    "⩘\0"
    "⩚\0"
    "∠\0"
    "⦤\0"
    "∠\0"
    "∡\0"
    "⦨\0"
    "⦩\0"
    "⦪\0"
    "⦫\0"
    "⦬\0"
    "⦭\0"
    "⦮\0"
    "⦯\0"
    "∟\0"
    "⊾\0"
    "⦝\0"
    "∢\0"
    "Å\0"
    "⍼\0"
    "ą\0"
    "𝕒\0"
    "≈\0"
    "⩰\0"
    "⩯\0"
    "≊\0"
    "≋\0"
    "'\0"
    "≈\0"
    "≊\0"
    "å\0"
    "𝒶\0"
    "*\0"
    "≈\0"
    "≍\0"
    "ã\0"
    "ä\0"
    "∳\0"
    "⨑\0"
    "⫭\0"
    "≌\0"
    "϶\0"
    "‵\0"
    "∽\0"
    "⋍\0"
    "⊽\0"
    "⌅\0"
    "⌅\0"
    "⎵\0"
    "⎶\0"
    "≌\0"
    "б\0"
    "„\0"
    "∵\0"
    "∵\0"
    "⦰\0"
    "϶\0"
    "ℬ\0"
    "β\0"
    "ℶ\0"
    "≬\0"
    "𝔟\0"
    "⋂\0"
    "◯\0"
    "⋃\0"
    "⨀\0"
    "⨁\0"
    "⨂\0"
    "⨆\0"
    "★\0"
    "▽\0"
    "△\0"
    "⨄\0"
    "⋁\0"
    "⋀\0"
    "⤍\0"
    "⧫\0"
    "▪\0"
    "▴\0"
    "▾\0"
    "◂\0"
    "▸\0"
    "␣\0"
    "▒\0"
    "░\0"
    "▓\0"
    "█\0"
    "=⃥\0"
    "≡⃥\0"
    "⌐\0"
    "𝕓\0"
    "⊥\0"
    "⊥\0"
    "⋈\0"
    "╗\0"
    "╔\0"
    "╖\0"
    "╓\0"
    "═\0"
    "╦\0"
    "╩\0"
    "╤\0"
    "╧\0"
    "╝\0"
    "╚\0"
    "╜\0"
    "╙\0"
    "║\0"
    "╬\0"
    "╣\0"
    "╠\0"
    "╫\0"
    "╢\0"
    "╟\0"
    "⧉\0"
    "╕\0"
    "╒\0"
    "┐\0"
    "┌\0"
    "─\0"
    "╥\0"
    "╨\0"
    "┬\0"
    "┴\0"
    "⊟\0"
    "⊞\0"
    "⊠\0"
    "╛\0"
    "╘\0"
    "┘\0"
    "└\0"
    "│\0"
    "╪\0"
    "╡\0"
    "╞\0"
    "┼\0"
    "┤\0"
    "├\0"
    "‵\0"
    "˘\0"
    "¦\0"
    "𝒷\0"
    "⁏\0"
    "∽\0"
    "⋍\0"
    "\\\0"
    "⧅\0"
    "⟈\0"
    "•\0"
    "•\0"
    "≎\0"
    "⪮\0"
    "≏\0"
    "≏\0"
    "ć\0"
    "∩\0"
    "⩄\0"
    "⩉\0"
    "⩋\0"
    "⩇\0"
    "⩀\0"
    "∩︀\0"
    "⁁\0"
    "ˇ\0"
    "⩍\0"
    "č\0"
    "ç\0"
    "ĉ\0"
    "⩌\0"
    "⩐\0"
    "ċ\0"
    "¸\0"
    "⦲\0"
    "¢\0"
    "·\0"
    "𝔠\0"
    "ч\0"
    "✓\0"
    "✓\0"
    "χ\0"
    "○\0"
    "⧃\0"
    "ˆ\0"
    "≗\0"
    "↺\0"
    "↻\0"
    "®\0"
    "Ⓢ\0"
    "⊛\0"
    "⊚\0"
    "⊝\0"
    "≗\0"
    "⨐\0"
    "⫯\0"
    "⧂\0"
    "♣\0"
    "♣\0"
    ":\0"
    "≔\0"
    "≔\0"
    ",\0"
    "@\0"
    "∁\0"
    "∘\0"
    "∁\0"
    "ℂ\0"
    "≅\0"
    "⩭\0"
    "∮\0"
    "𝕔\0"
    "∐\0"
    "©\0"
    "℗\0"
    "↵\0"
    "✗\0"
    "𝒸\0"
    "⫏\0"
    "⫑\0"
    "⫐\0"
    "⫒\0"
    "⋯\0"
    "⤸\0"
    "⤵\0"
    "⋞\0"
    "⋟\0"
    "↶\0"
    "⤽\0"
    "∪\0"
    "⩈\0"
    "⩆\0"
    "⩊\0"
    "⊍\0"
    "⩅\0"
    "∪︀\0"
    "↷\0"
    "⤼\0"
    "⋞\0"
    "⋟\0"
    "⋎\0"
    "⋏\0"
    "¤\0"
    "↶\0"
    "↷\0"
    "⋎\0"
    "⋏\0"
    "∲\0"
    "∱\0"
    "⌭\0"
    "⇓\0"
    "⥥\0"
    "†\0"
    "ℸ\0"
    "↓\0"
    "‐\0"
    "⊣\0"
    "⤏\0"
    "˝\0"
    "ď\0"
    "д\0"
    "ⅆ\0"
    "‡\0"
    "⇊\0"
    "⩷\0"
    "°\0"
    "δ\0"
    "⦱\0"
    "⥿\0"
    "𝔡\0"
    "⇃\0"
    "⇂\0"
    "⋄\0"
    "⋄\0"
    "♦\0"
    "♦\0"
    "¨\0"
    "ϝ\0"
    "⋲\0"
    "÷\0"
    "÷\0"
    "⋇\0"
    "⋇\0"
    "ђ\0"
    "⌞\0"
    "⌍\0"
    "$\0"
    "𝕕\0"
    "˙\0"
    "≐\0"
    "≑\0"
    "∸\0"
    "∔\0"
    "⊡\0"
    "⌆\0"
    "↓\0"
    "⇊\0"
    "⇃\0"
    "⇂\0"
    "⤐\0"
    "⌟\0"
    "⌌\0"
    "𝒹\0"
    "ѕ\0"
    "⧶\0"
    "đ\0"
    "⋱\0"
    "▿\0"
    "▾\0"
    "⇵\0"
    "⥯\0"
    "⦦\0"
    "џ\0"
    "⟿\0"
    "⩷\0"
    "≑\0"
    "é\0"
    "⩮\0"
    "ě\0"
    "≖\0"
    "ê\0"
    "≕\0"
    "э\0"
    "ė\0"
    "ⅇ\0"
    "≒\0"
    "𝔢\0"
    "⪚\0"
    "è\0"
    "⪖\0"
    "⪘\0"
    "⪙\0"
    "⏧\0"
    "ℓ\0"
    "⪕\0"
    "⪗\0"
    "ē\0"
    "∅\0"
    "∅\0"
    "∅\0"
    " \0"
    " \0"
    " \0"
    "ŋ\0"
    " \0"
    "ę\0"
    "𝕖\0"
    "⋕\0"
    "⧣\0"
    "⩱\0"
    "ε\0"
    "ε\0"
    "ϵ\0"
    "≖\0"
    "≕\0"
    "≂\0"
    "⪖\0"
    "⪕\0"
    "=\0"
    "≟\0"
    "≡\0"
    "⩸\0"
    "⧥\0"
    "≓\0"
    "⥱\0"
    "ℯ\0"
    "≐\0"
    "≂\0"
    "η\0"
    "ð\0"
    "ë\0"
    "€\0"
    "!\0"
    "∃\0"
    "ℰ\0"
    "ⅇ\0"
    "≒\0"
    "ф\0"
    "♀\0"
    "ﬃ\0"
    "ﬀ\0"
    "ﬄ\0"
    "𝔣\0"
    "ﬁ\0"
    "fj\0"
    "♭\0"
    "ﬂ\0"
    "▱\0"
    "ƒ\0"
    "𝕗\0"
    "∀\0"
    "⋔\0"
    "⫙\0"
    "⨍\0"
    "½\0"
    "⅓\0"
    "¼\0"
    "⅕\0"
    "⅙\0"
    "⅛\0"
    "⅔\0"
    "⅖\0"
    "¾\0"
    "⅗\0"
    "⅜\0"
    "⅘\0"
    "⅚\0"
    "⅝\0"
    "⅞\0"
    "⁄\0"
    "⌢\0"
    "𝒻\0"
    "≧\0"
    "⪌\0"
    "ǵ\0"
    "γ\0"
    "ϝ\0"
    "⪆\0"
    "ğ\0"
    "ĝ\0"
    "г\0"
    "ġ\0"
    "≥\0"
    "⋛\0"
    "≥\0"
    "≧\0"
    "⩾\0"
    "⩾\0"
    "⪩\0"
    "⪀\0"
    "⪂\0"
    "⪄\0"
    "⋛︀\0"
    "⪔\0"
    "𝔤\0"
    "≫\0"
    "⋙\0"
    "ℷ\0"
    "ѓ\0"
    "≷\0"
    "⪒\0"
    "⪥\0"
    "⪤\0"
    "≩\0"
    "⪊\0"
    "⪊\0"
    "⪈\0"
    "⪈\0"
    "≩\0"
    "⋧\0"
    "𝕘\0"
    "`\0"
    "ℊ\0"
    "≳\0"
    "⪎\0"
    "⪐\0"
    ">\0"
    "⪧\0"
    "⩺\0"
    "⋗\0"
    "⦕\0"
    "⩼\0"
    "⪆\0"
    "⥸\0"
    "⋗\0"
    "⋛\0"
    "⪌\0"
    "≷\0"
    "≳\0"
    "≩︀\0"
    "≩︀\0"
    "⇔\0"
    " \0"
    "½\0"
    "ℋ\0"
    "ъ\0"
    "↔\0"
    "⥈\0"
    "↭\0"
    "ℏ\0"
    "ĥ\0"
    "♥\0"
    "♥\0"
    "…\0"
    "⊹\0"
    "𝔥\0"
    "⤥\0"
    "⤦\0"
    "⇿\0"
    "∻\0"
    "↩\0"
    "↪\0"
    "𝕙\0"
    "―\0"
    "𝒽\0"
    "ℏ\0"
    "ħ\0"
    "⁃\0"
    "‐\0"
    "í\0"
    "⁣\0"
    "î\0"
    "и\0"
    "е\0"
    "¡\0"
    "⇔\0"
    "𝔦\0"
    "ì\0"
    "ⅈ\0"
    "⨌\0"
    "∭\0"
    "⧜\0"
    "℩\0"
    "ĳ\0"
    "ī\0"
    "ℑ\0"
    "ℐ\0"
    "ℑ\0"
    "ı\0"
    "⊷\0"
    "Ƶ\0"
    "∈\0"
    "℅\0"
    "∞\0"
    "⧝\0"
    "ı\0"
    "∫\0"
    "⊺\0"
    "ℤ\0"
    "⊺\0"
    "⨗\0"
    "⨼\0"
    "ё\0"
    "į\0"
    "𝕚\0"
    "ι\0"
    "⨼\0"
    "¿\0"
    "𝒾\0"
    "∈\0"
    "⋹\0"
    "⋵\0"
    "⋴\0"
    "⋳\0"
    "∈\0"
    "⁢\0"
    "ĩ\0"
    "і\0"
    "ï\0"
    "ĵ\0"
    "й\0"
    "𝔧\0"
    "ȷ\0"
    "𝕛\0"
    "𝒿\0"
    "ј\0"
    "є\0"
    "κ\0"
    "ϰ\0"
    "ķ\0"
    "к\0"
    "𝔨\0"
    "ĸ\0"
    "х\0"
    "ќ\0"
    "𝕜\0"
    "𝓀\0"
    "⇚\0"
    "⇐\0"
    "⤛\0"
    "⤎\0"
    "≦\0"
    "⪋\0"
    "⥢\0"
    "ĺ\0"
    "⦴\0"
    "ℒ\0"
    "λ\0"
    "⟨\0"
    "⦑\0"
    "⟨\0"
    "⪅\0"
    "«\0"
    "←\0"
    "⇤\0"
    "⤟\0"
    "⤝\0"
    "↩\0"
    "↫\0"
    "⤹\0"
    "⥳\0"
    "↢\0"
    "⪫\0"
    "⤙\0"
    "⪭\0"
    "⪭︀\0"
    "⤌\0"
    "❲\0"
    "{\0"
    "[\0"
    "⦋\0"
    "⦏\0"
    "⦍\0"
    "ľ\0"
    "ļ\0"
    "⌈\0"
    "{\0"
    "л\0"
    "⤶\0"
    "“\0"
    "„\0"
    "⥧\0"
    "⥋\0"
    "↲\0"
    "≤\0"
    "←\0"
    "↢\0"
    "↽\0"
    "↼\0"
    "⇇\0"
    "↔\0"
    "⇆\0"
    "⇋\0"
    "↭\0"
    "⋋\0"
    "⋚\0"
    "≤\0"
    "≦\0"
    "⩽\0"
    "⩽\0"
    "⪨\0"
    "⩿\0"
    "⪁\0"
    "⪃\0"
    "⋚︀\0"
    "⪓\0"
    "⪅\0"
    "⋖\0"
    "⋚\0"
    "⪋\0"
    "≶\0"
    "≲\0"
    "⥼\0"
    "⌊\0"
    "𝔩\0"
    "≶\0"
    "⪑\0"
    "↽\0"
    "↼\0"
    "⥪\0"
    "▄\0"
    "љ\0"
    "≪\0"
    "⇇\0"
    "⌞\0"
    "⥫\0"
    "◺\0"
    "ŀ\0"
    "⎰\0"
    "⎰\0"
    "≨\0"
    "⪉\0"
    "⪉\0"
    "⪇\0"
    "⪇\0"
    "≨\0"
    "⋦\0"
    "⟬\0"
    "⇽\0"
    "⟦\0"
    "⟵\0"
    "⟷\0"
    "⟼\0"
    "⟶\0"
    "↫\0"
    "↬\0"
    "⦅\0"
    "𝕝\0"
    "⨭\0"
    "⨴\0"
    "∗\0"
    "_\0"
    "◊\0"
    "◊\0"
    "⧫\0"
    "(\0"
    "⦓\0"
    "⇆\0"
    "⌟\0"
    "⇋\0"
    "⥭\0"
    "\u200E\0"
    "⊿\0"
    "‹\0"
    "𝓁\0"
    "↰\0"
    "≲\0"
    "⪍\0"
    "⪏\0"
    "[\0"
    "‘\0"
    "‚\0"
    "ł\0"
    "<\0"
    "⪦\0"
    "⩹\0"
    "⋖\0"
    "⋋\0"
    "⋉\0"
    "⥶\0"
    "⩻\0"
    "⦖\0"
    "◃\0"
    "⊴\0"
    "◂\0"
    "⥊\0"
    "⥦\0"
    "≨︀\0"
    "≨︀\0"
    "∺\0"
    "¯\0"
    "♂\0"
    "✠\0"
    "✠\0"
    "↦\0"
    "↦\0"
    "↧\0"
    "↤\0"
    "↥\0"
    "▮\0"
    "⨩\0"
    "м\0"
    "—\0"
    "∡\0"
    "𝔪\0"
    "℧\0"
    "µ\0"
    "∣\0"
    "*\0"
    "⫰\0"
    "·\0"
    "−\0"
    "⊟\0"
    "∸\0"
    "⨪\0"
    "⫛\0"
    "…\0"
    "∓\0"
    "⊧\0"
    "𝕞\0"
    "∓\0"
    "𝓂\0"
    "∾\0"
    "μ\0"
    "⊸\0"
    "⊸\0"
    "⋙̸\0"
    "≫⃒\0"
    "≫̸\0"
    "⇍\0"
    "⇎\0"
    "⋘̸\0"
    "≪⃒\0"
    "≪̸\0"
    "⇏\0"
    "⊯\0"
    "⊮\0"
    "∇\0"
    "ń\0"
    "∠⃒\0"
    "≉\0"
    "⩰̸\0"
    "≋̸\0"
    "ŉ\0"
    "≉\0"
    "♮\0"
    "♮\0"
    "ℕ\0"
    " \0"
    "≎̸\0"
    "≏̸\0"
    "⩃\0"
    "ň\0"
    "ņ\0"
    "≇\0"
    "⩭̸\0"
    "⩂\0"
    "н\0"
    "–\0"
    "≠\0"
    "⇗\0"
    "⤤\0"
    "↗\0"
    "↗\0"
    "≐̸\0"
    "≢\0"
    "⤨\0"
    "≂̸\0"
    "∄\0"
    "∄\0"
    "𝔫\0"
    "≧̸\0"
    "≱\0"
    "≱\0"
    "≧̸\0"
    "⩾̸\0"
    "⩾̸\0"
    "≵\0"
    "≯\0"
    "≯\0"
    "⇎\0"
    "↮\0"
    "⫲\0"
    "∋\0"
    "⋼\0"
    "⋺\0"
    "∋\0"
    "њ\0"
    "⇍\0"
    "≦̸\0"
    "↚\0"
    "‥\0"
    "≰\0"
    "↚\0"
    "↮\0"
    "≰\0"
    "≦̸\0"
    "⩽̸\0"
    "⩽̸\0"
    "≮\0"
    "≴\0"
    "≮\0"
    "⋪\0"
    "⋬\0"
    "∤\0"
    "𝕟\0"
    "¬\0"
    "∉\0"
    "⋹̸\0"
    "⋵̸\0"
    "∉\0"
    "⋷\0"
    "⋶\0"
    "∌\0"
    "∌\0"
    "⋾\0"
    "⋽\0"
    "∦\0"
    "∦\0"
    "⫽⃥\0"
    "∂̸\0"
    "⨔\0"
    "⊀\0"
    "⋠\0"
    "⪯̸\0"
    "⊀\0"
    "⪯̸\0"
    "⇏\0"
    "↛\0"
    "⤳̸\0"
    "↝̸\0"
    "↛\0"
    "⋫\0"
    "⋭\0"
    "⊁\0"
    "⋡\0"
    "⪰̸\0"
    "𝓃\0"
    "∤\0"
    "∦\0"
    "≁\0"
    "≄\0"
    "≄\0"
    "∤\0"
    "∦\0"
    "⋢\0"
    "⋣\0"
    "⊄\0"
    "⫅̸\0"
    "⊈\0"
    "⊂⃒\0"
    "⊈\0"
    "⫅̸\0"
    "⊁\0"
    "⪰̸\0"
    "⊅\0"
    "⫆̸\0"
    "⊉\0"
    "⊃⃒\0"
    "⊉\0"
    "⫆̸\0"
    "≹\0"
    "ñ\0"
    "≸\0"
    "⋪\0"
    "⋬\0"
    "⋫\0"
    "⋭\0"
    "ν\0"
    "#\0"
    "№\0"
    " \0"
    "⊭\0"
    "⤄\0"
    "≍⃒\0"
    "⊬\0"
    "≥⃒\0"
    ">⃒\0"
    "⧞\0"
    "⤂\0"
    "≤⃒\0"
    "<⃒\0"
    "⊴⃒\0"
    "⤃\0"
    "⊵⃒\0"
    "∼⃒\0"
    "⇖\0"
    "⤣\0"
    "↖\0"
    "↖\0"
    "⤧\0"
    "Ⓢ\0"
    "ó\0"
    "⊛\0"
    "⊚\0"
    "ô\0"
    "о\0"
    "⊝\0"
    "ő\0"
    "⨸\0"
    "⊙\0"
    "⦼\0"
    "œ\0"
    "⦿\0"
    "𝔬\0"
    "˛\0"
    "ò\0"
    "⧁\0"
    "⦵\0"
    "Ω\0"
    "∮\0"
    "↺\0"
    "⦾\0"
    "⦻\0"
    "‾\0"
    "⧀\0"
    "ō\0"
    "ω\0"
    "ο\0"
    "⦶\0"
    "⊖\0"
    "𝕠\0"
    "⦷\0"
    "⦹\0"
    "⊕\0"
    "∨\0"
    "↻\0"
    "⩝\0"
    "ℴ\0"
    "ℴ\0"
    "ª\0"
    "º\0"
    "⊶\0"
    "⩖\0"
    "⩗\0"
    "⩛\0"
    "ℴ\0"
    "ø\0"
    "⊘\0"
    "õ\0"
    "⊗\0"
    "⨶\0"
    "ö\0"
    "⌽\0"
    "∥\0"
    "¶\0"
    "∥\0"
    "⫳\0"
    "⫽\0"
    "∂\0"
    "п\0"
    "%\0"
    ".\0"
    "‰\0"
    "⊥\0"
    "‱\0"
    "𝔭\0"
    "φ\0"
    "ϕ\0"
    "ℳ\0"
    "☎\0"
    "π\0"
    "⋔\0"
    "ϖ\0"
    "ℏ\0"
    "ℎ\0"
    "ℏ\0"
    "+\0"
    "⨣\0"
    "⊞\0"
    "⨢\0"
    "∔\0"
    "⨥\0"
    "⩲\0"
    "±\0"
    "⨦\0"
    "⨧\0"
    "±\0"
    "⨕\0"
    "𝕡\0"
    "£\0"
    "≺\0"
    "⪳\0"
    "⪷\0"
    "≼\0"
    "⪯\0"
    "≺\0"
    "⪷\0"
    "≼\0"
    "⪯\0"
    "⪹\0"
    "⪵\0"
    "⋨\0"
    "≾\0"
    "′\0"
    "ℙ\0"
    "⪵\0"
    "⪹\0"
    "⋨\0"
    "∏\0"
    "⌮\0"
    "⌒\0"
    "⌓\0"
    "∝\0"
    "∝\0"
    "≾\0"
    "⊰\0"
    "𝓅\0"
    "ψ\0"
    " \0"
    "𝔮\0"
    "⨌\0"
    "𝕢\0"
    "⁗\0"
    "𝓆\0"
    "ℍ\0"
    "⨖\0"
    "?\0"
    "≟\0"
    "\"\0"
    "⇛\0"
    "⇒\0"
    "⤜\0"
    "⤏\0"
    "⥤\0"
    "∽̱\0"
    "ŕ\0"
    "√\0"
    "⦳\0"
    "⟩\0"
    "⦒\0"
    "⦥\0"
    "⟩\0"
    "»\0"
    "→\0"
    "⥵\0"
    "⇥\0"
    "⤠\0"
    "⤳\0"
    "⤞\0"
    "↪\0"
    "↬\0"
    "⥅\0"
    "⥴\0"
    "↣\0"
    "↝\0"
    "⤚\0"
    "∶\0"
    "ℚ\0"
    "⤍\0"
    "❳\0"
    "}\0"
    "]\0"
    "⦌\0"
    "⦎\0"
    "⦐\0"
    "ř\0"
    "ŗ\0"
    "⌉\0"
    "}\0"
    "р\0"
    "⤷\0"
    "⥩\0"
    "”\0"
    "”\0"
    "↳\0"
    "ℜ\0"
    "ℛ\0"
    "ℜ\0"
    "ℝ\0"
    "▭\0"
    "®\0"
    "⥽\0"
    "⌋\0"
    "𝔯\0"
    "⇁\0"
    "⇀\0"
    "⥬\0"
    "ρ\0"
    "ϱ\0"
    "→\0"
    "↣\0"
    "⇁\0"
    "⇀\0"
    "⇄\0"
    "⇌\0"
    "⇉\0"
    "↝\0"
    "⋌\0"
    "˚\0"
    "≓\0"
    "⇄\0"
    "⇌\0"
    "\u200F\0"
    "⎱\0"
    "⎱\0"
    "⫮\0"
    "⟭\0"
    "⇾\0"
    "⟧\0"
    "⦆\0"
    "𝕣\0"
    "⨮\0"
    "⨵\0"
    ")\0"
    "⦔\0"
    "⨒\0"
    "⇉\0"
    "›\0"
    "𝓇\0"
    "↱\0"
    "]\0"
    "’\0"
    "’\0"
    "⋌\0"
    "⋊\0"
    "▹\0"
    "⊵\0"
    "▸\0"
    "⧎\0"
    "⥨\0"
    "℞\0"
    "ś\0"
    "‚\0"
    "≻\0"
    "⪴\0"
    "⪸\0"
    "š\0"
    "≽\0"
    "⪰\0"
    "ş\0"
    "ŝ\0"
    "⪶\0"
    "⪺\0"
    "⋩\0"
    "⨓\0"
    "≿\0"
    "с\0"
    "⋅\0"
    "⊡\0"
    "⩦\0"
    "⇘\0"
    "⤥\0"
    "↘\0"
    "↘\0"
    "§\0"
    ";\0"
    "⤩\0"
    "∖\0"
    "∖\0"
    "✶\0"
    "𝔰\0"
    "⌢\0"
    "♯\0"
    "щ\0"
    "ш\0"
    "∣\0"
    "∥\0"
    "­\0"
    "σ\0"
    "ς\0"
    "ς\0"
    "∼\0"
    "⩪\0"
    "≃\0"
    "≃\0"
    "⪞\0"
    "⪠\0"
    "⪝\0"
    "⪟\0"
    "≆\0"
    "⨤\0"
    "⥲\0"
    "←\0"
    "∖\0"
    "⨳\0"
    "⧤\0"
    "∣\0"
    "⌣\0"
    "⪪\0"
    "⪬\0"
    "⪬︀\0"
    "ь\0"
    "/\0"
    "⧄\0"
    "⌿\0"
    "𝕤\0"
    "♠\0"
    "♠\0"
    "∥\0"
    "⊓\0"
    "⊓︀\0"
    "⊔\0"
    "⊔︀\0"
    "⊏\0"
    "⊑\0"
    "⊏\0"
    "⊑\0"
    "⊐\0"
    "⊒\0"
    "⊐\0"
    "⊒\0"
    "□\0"
    "□\0"
    "▪\0"
    "▪\0"
    "→\0"
    "𝓈\0"
    "∖\0"
    "⌣\0"
    "⋆\0"
    "☆\0"
    "★\0"
    "ϵ\0"
    "ϕ\0"
    "¯\0"
    "⊂\0"
    "⫅\0"
    "⪽\0"
    "⊆\0"
    "⫃\0"
    "⫁\0"
    "⫋\0"
    "⊊\0"
    "⪿\0"
    "⥹\0"
    "⊂\0"
    "⊆\0"
    "⫅\0"
    "⊊\0"
    "⫋\0"
    "⫇\0"
    "⫕\0"
    "⫓\0"
    "≻\0"
    "⪸\0"
    "≽\0"
    "⪰\0"
    "⪺\0"
    "⪶\0"
    "⋩\0"
    "≿\0"
    "∑\0"
    "♪\0"
    "⊃\0"
    "¹\0"
    "²\0"
    "³\0"
    "⫆\0"
    "⪾\0"
    "⫘\0"
    "⊇\0"
    "⫄\0"
    "⟉\0"
    "⫗\0"
    "⥻\0"
    "⫂\0"
    "⫌\0"
    "⊋\0"
    "⫀\0"
    "⊃\0"
    "⊇\0"
    "⫆\0"
    "⊋\0"
    "⫌\0"
    "⫈\0"
    "⫔\0"
    "⫖\0"
    "⇙\0"
    "⤦\0"
    "↙\0"
    "↙\0"
    "⤪\0"
    "ß\0"
    "⌖\0"
    "τ\0"
    "⎴\0"
    "ť\0"
    "ţ\0"
    "т\0"
    "⃛\0"
    "⌕\0"
    "𝔱\0"
    "∴\0"
    "∴\0"
    "θ\0"
    "ϑ\0"
    "ϑ\0"
    "≈\0"
    "∼\0"
    " \0"
    "≈\0"
    "∼\0"
    "þ\0"
    "˜\0"
    "×\0"
    "⊠\0"
    "⨱\0"
    "⨰\0"
    "∭\0"
    "⤨\0"
    "⊤\0"
    "⌶\0"
    "⫱\0"
    "𝕥\0"
    "⫚\0"
    "⤩\0"
    "‴\0"
    "™\0"
    "▵\0"
    "▿\0"
    "◃\0"
    "⊴\0"
    "≜\0"
    "▹\0"
    "⊵\0"
    "◬\0"
    "≜\0"
    "⨺\0"
    "⨹\0"
    "⧍\0"
    "⨻\0"
    "⏢\0"
    "𝓉\0"
    "ц\0"
    "ћ\0"
    "ŧ\0"
    "≬\0"
    "↞\0"
    "↠\0"
    "⇑\0"
    "⥣\0"
    "ú\0"
    "↑\0"
    "ў\0"
    "ŭ\0"
    "û\0"
    "у\0"
    "⇅\0"
    "ű\0"
    "⥮\0"
    "⥾\0"
    "𝔲\0"
    "ù\0"
    "↿\0"
    "↾\0"
    "▀\0"
    "⌜\0"
    "⌜\0"
    "⌏\0"
    "◸\0"
    "ū\0"
    "¨\0"
    "ų\0"
    "𝕦\0"
    "↑\0"
    "↕\0"
    "↿\0"
    "↾\0"
    "⊎\0"
    "υ\0"
    "ϒ\0"
    "υ\0"
    "⇈\0"
    "⌝\0"
    "⌝\0"
    "⌎\0"
    "ů\0"
    "◹\0"
    "𝓊\0"
    "⋰\0"
    "ũ\0"
    "▵\0"
    "▴\0"
    "⇈\0"
    "ü\0"
    "⦧\0"
    "⇕\0"
    "⫨\0"
    "⫩\0"
    "⊨\0"
    "⦜\0"
    "ϵ\0"
    "ϰ\0"
    "∅\0"
    "ϕ\0"
    "ϖ\0"
    "∝\0"
    "↕\0"
    "ϱ\0"
    "ς\0"
    "⊊︀\0"
    "⫋︀\0"
    "⊋︀\0"
    "⫌︀\0"
    "ϑ\0"
    "⊲\0"
    "⊳\0"
    "в\0"
    "⊢\0"
    "∨\0"
    "⊻\0"
    "≚\0"
    "⋮\0"
    "|\0"
    "|\0"
    "𝔳\0"
    "⊲\0"
    "⊂⃒\0"
    "⊃⃒\0"
    "𝕧\0"
    "∝\0"
    "⊳\0"
    "𝓋\0"
    "⫋︀\0"
    "⊊︀\0"
    "⫌︀\0"
    "⊋︀\0"
    "⦚\0"
    "ŵ\0"
    "⩟\0"
    "∧\0"
    "≙\0"
    "℘\0"
    "𝔴\0"
    "𝕨\0"
    "℘\0"
    "≀\0"
    "≀\0"
    "𝓌\0"
    "⋂\0"
    "◯\0"
    "⋃\0"
    "▽\0"
    "𝔵\0"
    "⟺\0"
    "⟷\0"
    "ξ\0"
    "⟸\0"
    "⟵\0"
    "⟼\0"
    "⋻\0"
    "⨀\0"
    "𝕩\0"
    "⨁\0"
    "⨂\0"
    "⟹\0"
    "⟶\0"
    "𝓍\0"
    "⨆\0"
    "⨄\0"
    "△\0"
    "⋁\0"
    "⋀\0"
    "ý\0"
    "я\0"
    "ŷ\0"
    "ы\0"
    "¥\0"
    "𝔶\0"
    "ї\0"
    "𝕪\0"
    "𝓎\0"
    "ю\0"
    "ÿ\0"
    "ź\0"
    "ž\0"
    "з\0"
    "ż\0"
    "ℨ\0"
    "ζ\0"
    "𝔷\0"
    "ж\0"
    "⇝\0"
    "𝕫\0"
    "𝓏\0"
    "‍\0"
    "‌\0";

const CharacterReference kCharacterReferences[2125] = {
    {0, 0},        // AElig
    {6, 3},        // AMP
    {10, 5},       // Aacute
    {17, 8},       // Abreve
    {24, 11},      // Acirc
    {30, 14},      // Acy
    {34, 17},      // Afr
    {38, 22},      // Agrave
    {45, 25},      // Alpha
    {51, 28},      // Amacr
    {57, 31},      // And
    {61, 35},      // Aogon
    {67, 38},      // Aopf
    {72, 43},      // ApplyFunction
    {86, 47},      // Aring
    {92, 50},      // Ascr
    {97, 55},      // Assign
    {104, 59},     // Atilde
    {111, 62},     // Auml
    {116, 65},     // Backslash
    {126, 69},     // Barv
    {131, 73},     // Barwed
    {138, 77},     // Bcy
    {142, 80},     // Because
    {150, 84},     // Bernoullis
    {161, 88},     // Beta
    {166, 91},     // Bfr
    {170, 96},     // Bopf
    {175, 101},    // Breve
    {181, 104},    // Bscr
    {186, 108},    // Bumpeq
    {193, 112},    // CHcy
    {198, 115},    // COPY
    {203, 118},    // Cacute
    {210, 121},    // Cap
    {214, 125},    // CapitalDifferentialD
    {235, 129},    // Cayleys
    {243, 133},    // Ccaron
    {250, 136},    // Ccedil
    {257, 139},    // Ccirc
    {263, 142},    // Cconint
    {271, 146},    // Cdot
    {276, 149},    // Cedilla
    {284, 152},    // CenterDot
    {294, 155},    // Cfr
    {298, 159},    // Chi
    {302, 162},    // CircleDot
    {312, 166},    // CircleMinus
    {324, 170},    // CirclePlus
    {335, 174},    // CircleTimes
    {347, 178},    // ClockwiseContourIntegral
    {372, 182},    // CloseCurlyDoubleQuote
    {394, 186},    // CloseCurlyQuote
    {410, 190},    // Colon
    {416, 194},    // Colone
    {423, 198},    // Congruent
    {433, 202},    // Conint
    {440, 206},    // ContourIntegral
    {456, 210},    // Copf
    {461, 214},    // Coproduct
    {471, 218},    // CounterClockwiseContourIntegral
    {503, 222},    // Cross
    {509, 226},    // Cscr
    {514, 231},    // Cup
    {518, 235},    // CupCap
    {525, 239},    // DD
    {528, 243},    // DDotrahd
    {537, 247},    // DJcy
    {542, 250},    // DScy
    {547, 253},    // DZcy
    {552, 256},    // Dagger
    {559, 260},    // Darr
    {564, 264},    // Dashv
    {570, 268},    // Dcaron
    {577, 271},    // Dcy
    {581, 274},    // Del
    {585, 278},    // Delta
    {591, 281},    // Dfr
    {595, 286},    // DiacriticalAcute
    {612, 289},    // DiacriticalDot
    {627, 292},    // DiacriticalDoubleAcute
    {650, 295},    // DiacriticalGrave
    {667, 297},    // DiacriticalTilde
    {684, 300},    // Diamond
    {692, 304},    // DifferentialD
    {706, 308},    // Dopf
    {711, 313},    // Dot
    {715, 316},    // DotDot
    {722, 320},    // DotEqual
    {731, 324},    // DoubleContourIntegral
    {753, 328},    // DoubleDot
    {763, 331},    // DoubleDownArrow
    {779, 335},    // DoubleLeftArrow
    {795, 339},    // DoubleLeftRightArrow
    {816, 343},    // DoubleLeftTee
    {830, 347},    // DoubleLongLeftArrow
    {850, 351},    // DoubleLongLeftRightArrow
    {875, 355},    // DoubleLongRightArrow
    {896, 359},    // DoubleRightArrow
    {913, 363},    // DoubleRightTee
    {928, 367},    // DoubleUpArrow
    {942, 371},    // DoubleUpDownArrow
    {960, 375},    // DoubleVerticalBar
    {978, 379},    // DownArrow
    {988, 383},    // DownArrowBar
    {1001, 387},   // DownArrowUpArrow
    {1018, 391},   // DownBreve
    {1028, 394},   // DownLeftRightVector
    {1048, 398},   // DownLeftTeeVector
    {1066, 402},   // DownLeftVector
    {1081, 406},   // DownLeftVectorBar
    {1099, 410},   // DownRightTeeVector
    {1118, 414},   // DownRightVector
    {1134, 418},   // DownRightVectorBar
    {1153, 422},   // DownTee
    {1161, 426},   // DownTeeArrow
    {1174, 430},   // Downarrow
    {1184, 434},   // Dscr
    {1189, 439},   // Dstrok
    {1196, 442},   // ENG
    {1200, 445},   // ETH
    {1204, 448},   // Eacute
    {1211, 451},   // Ecaron
    {1218, 454},   // Ecirc
    {1224, 457},   // Ecy
    {1228, 460},   // Edot
    {1233, 463},   // Efr
    {1237, 468},   // Egrave
    {1244, 471},   // Element
    {1252, 475},   // Emacr
    {1258, 478},   // EmptySmallSquare
    {1275, 482},   // EmptyVerySmallSquare
    {1296, 486},   // Eogon
    {1302, 489},   // Eopf
    {1307, 494},   // Epsilon
    {1315, 497},   // Equal
    {1321, 501},   // EqualTilde
    {1332, 505},   // Equilibrium
    {1344, 509},   // Escr
    {1349, 513},   // Esim
    {1354, 517},   // Eta
    {1358, 520},   // Euml
    {1363, 523},   // Exists
    {1370, 527},   // ExponentialE
    {1383, 531},   // Fcy
    {1387, 534},   // Ffr
    {1391, 539},   // FilledSmallSquare
    {1409, 543},   // FilledVerySmallSquare
    {1431, 547},   // Fopf
    {1436, 552},   // ForAll
    {1443, 556},   // Fouriertrf
    {1454, 560},   // Fscr
    {1459, 564},   // GJcy
    {1464, 567},   // GT
    {1467, 569},   // Gamma
    {1473, 572},   // Gammad
    {1480, 575},   // Gbreve
    {1487, 578},   // Gcedil
    {1494, 581},   // Gcirc
    {1500, 584},   // Gcy
    {1504, 587},   // Gdot
    {1509, 590},   // Gfr
    {1513, 595},   // Gg
    {1516, 599},   // Gopf
    {1521, 604},   // GreaterEqual
    {1534, 608},   // GreaterEqualLess
    {1551, 612},   // GreaterFullEqual
    {1568, 616},   // GreaterGreater
    {1583, 620},   // GreaterLess
    {1595, 624},   // GreaterSlantEqual
    {1613, 628},   // GreaterTilde
    {1626, 632},   // Gscr
    {1631, 637},   // Gt
    {1634, 641},   // HARDcy
    {1641, 644},   // Hacek
    {1647, 647},   // Hat
    {1651, 649},   // Hcirc
    {1657, 652},   // Hfr
    {1661, 656},   // HilbertSpace
    {1674, 660},   // Hopf
    {1679, 664},   // HorizontalLine
    {1694, 668},   // Hscr
    {1699, 672},   // Hstrok
    {1706, 675},   // HumpDownHump
    {1719, 679},   // HumpEqual
    {1729, 683},   // IEcy
    {1734, 686},   // IJlig
    {1740, 689},   // IOcy
    {1745, 692},   // Iacute
    {1752, 695},   // Icirc
    {1758, 698},   // Icy
    {1762, 701},   // Idot
    {1767, 704},   // Ifr
    {1771, 708},   // Igrave
    {1778, 711},   // Im
    {1781, 715},   // Imacr
    {1787, 718},   // ImaginaryI
    {1798, 722},   // Implies
    {1806, 726},   // Int
    {1810, 730},   // Integral
    {1819, 734},   // Intersection
    {1832, 738},   // InvisibleComma
    {1847, 742},   // InvisibleTimes
    {1862, 746},   // Iogon
    {1868, 749},   // Iopf
    {1873, 754},   // Iota
    {1878, 757},   // Iscr
    {1883, 761},   // Itilde
    {1890, 764},   // Iukcy
    {1896, 767},   // Iuml
    {1901, 770},   // Jcirc
    {1907, 773},   // Jcy
    {1911, 776},   // Jfr
    {1915, 781},   // Jopf
    {1920, 786},   // Jscr
    {1925, 791},   // Jsercy
    {1932, 794},   // Jukcy
    {1938, 797},   // KHcy
    {1943, 800},   // KJcy
    {1948, 803},   // Kappa
    {1954, 806},   // Kcedil
    {1961, 809},   // Kcy
    {1965, 812},   // Kfr
    {1969, 817},   // Kopf
    {1974, 822},   // Kscr
    {1979, 827},   // LJcy
    {1984, 830},   // LT
    {1987, 832},   // Lacute
    {1994, 835},   // Lambda
    {2001, 838},   // Lang
    {2006, 842},   // Laplacetrf
    {2017, 846},   // Larr
    {2022, 850},   // Lcaron
    {2029, 853},   // Lcedil
    {2036, 856},   // Lcy
    {2040, 859},   // LeftAngleBracket
    {2057, 863},   // LeftArrow
    {2067, 867},   // LeftArrowBar
    {2080, 871},   // LeftArrowRightArrow
    {2100, 875},   // LeftCeiling
    {2112, 879},   // LeftDoubleBracket
    {2130, 883},   // LeftDownTeeVector
    {2148, 887},   // LeftDownVector
    {2163, 891},   // LeftDownVectorBar
    {2181, 895},   // LeftFloor
    {2191, 899},   // LeftRightArrow
    {2206, 903},   // LeftRightVector
    {2222, 907},   // LeftTee
    {2230, 911},   // LeftTeeArrow
    {2243, 915},   // LeftTeeVector
    {2257, 919},   // LeftTriangle
    {2270, 923},   // LeftTriangleBar
    {2286, 927},   // LeftTriangleEqual
    {2304, 931},   // LeftUpDownVector
    {2321, 935},   // LeftUpTeeVector
    {2337, 939},   // LeftUpVector
    {2350, 943},   // LeftUpVectorBar
    {2366, 947},   // LeftVector
    {2377, 951},   // LeftVectorBar
    {2391, 955},   // Leftarrow
    {2401, 959},   // Leftrightarrow
    {2416, 963},   // LessEqualGreater
    {2433, 967},   // LessFullEqual
    {2447, 971},   // LessGreater
    {2459, 975},   // LessLess
    {2468, 979},   // LessSlantEqual
    {2483, 983},   // LessTilde
    {2493, 987},   // Lfr
    {2497, 992},   // Ll
    {2500, 996},   // Lleftarrow
    {2511, 1000},  // Lmidot
    {2518, 1003},  // LongLeftArrow
    {2532, 1007},  // LongLeftRightArrow
    {2551, 1011},  // LongRightArrow
    {2566, 1015},  // Longleftarrow
    {2580, 1019},  // Longleftrightarrow
    {2599, 1023},  // Longrightarrow
    {2614, 1027},  // Lopf
    {2619, 1032},  // LowerLeftArrow
    {2634, 1036},  // LowerRightArrow
    {2650, 1040},  // Lscr
    {2655, 1044},  // Lsh
    {2659, 1048},  // Lstrok
    {2666, 1051},  // Lt
    {2669, 1055},  // Map
    {2673, 1059},  // Mcy
    {2677, 1062},  // MediumSpace
    {2689, 1066},  // Mellintrf
    {2699, 1070},  // Mfr
    {2703, 1075},  // MinusPlus
    {2713, 1079},  // Mopf
    {2718, 1084},  // Mscr
    {2723, 1088},  // Mu
    {2726, 1091},  // NJcy
    {2731, 1094},  // Nacute
    {2738, 1097},  // Ncaron
    {2745, 1100},  // Ncedil
    {2752, 1103},  // Ncy
    {2756, 1106},  // NegativeMediumSpace
    {2776, 1110},  // NegativeThickSpace
    {2795, 1114},  // NegativeThinSpace
    {2813, 1118},  // NegativeVeryThinSpace
    {2835, 1122},  // NestedGreaterGreater
    {2856, 1126},  // NestedLessLess
    {2871, 1130},  // NewLine
    {2879, 1132},  // Nfr
    {2883, 1137},  // NoBreak
    {2891, 1141},  // NonBreakingSpace
    {2908, 1144},  // Nopf
    {2913, 1148},  // Not
    {2917, 1152},  // NotCongruent
    {2930, 1156},  // NotCupCap
    {2940, 1160},  // NotDoubleVerticalBar
    {2961, 1164},  // NotElement
    {2972, 1168},  // NotEqual
    {2981, 1172},  // NotEqualTilde
    {2995, 1178},  // NotExists
    {3005, 1182},  // NotGreater
    {3016, 1186},  // NotGreaterEqual
    {3032, 1190},  // NotGreaterFullEqual
    {3052, 1196},  // NotGreaterGreater
    {3070, 1202},  // NotGreaterLess
    {3085, 1206},  // NotGreaterSlantEqual
    {3106, 1212},  // NotGreaterTilde
    {3122, 1216},  // NotHumpDownHump
    {3138, 1222},  // NotHumpEqual
    {3151, 1228},  // NotLeftTriangle
    {3167, 1232},  // NotLeftTriangleBar
    {3186, 1238},  // NotLeftTriangleEqual
    {3207, 1242},  // NotLess
    {3215, 1246},  // NotLessEqual
    {3228, 1250},  // NotLessGreater
    {3243, 1254},  // NotLessLess
    {3255, 1260},  // NotLessSlantEqual
    {3273, 1266},  // NotLessTilde
    {3286, 1270},  // NotNestedGreaterGreater
    {3310, 1276},  // NotNestedLessLess
    {3328, 1282},  // NotPrecedes
    {3340, 1286},  // NotPrecedesEqual
    {3357, 1292},  // NotPrecedesSlantEqual
    {3379, 1296},  // NotReverseElement
    {3397, 1300},  // NotRightTriangle
    {3414, 1304},  // NotRightTriangleBar
    {3434, 1310},  // NotRightTriangleEqual
    {3456, 1314},  // NotSquareSubset
    {3472, 1320},  // NotSquareSubsetEqual
    {3493, 1324},  // NotSquareSuperset
    {3511, 1330},  // NotSquareSupersetEqual
    {3534, 1334},  // NotSubset
    {3544, 1341},  // NotSubsetEqual
    {3559, 1345},  // NotSucceeds
    {3571, 1349},  // NotSucceedsEqual
    {3588, 1355},  // NotSucceedsSlantEqual
    {3610, 1359},  // NotSucceedsTilde
    {3627, 1365},  // NotSuperset
    {3639, 1372},  // NotSupersetEqual
    {3656, 1376},  // NotTilde
    {3665, 1380},  // NotTildeEqual
    {3679, 1384},  // NotTildeFullEqual
    {3697, 1388},  // NotTildeTilde
    {3711, 1392},  // NotVerticalBar
    {3726, 1396},  // Nscr
    {3731, 1401},  // Ntilde
    {3738, 1404},  // Nu
    {3741, 1407},  // OElig
    {3747, 1410},  // Oacute
    {3754, 1413},  // Ocirc
    {3760, 1416},  // Ocy
    {3764, 1419},  // Odblac
    {3771, 1422},  // Ofr
    {3775, 1427},  // Ograve
    {3782, 1430},  // Omacr
    {3788, 1433},  // Omega
    {3794, 1436},  // Omicron
    {3802, 1439},  // Oopf
    {3807, 1444},  // OpenCurlyDoubleQuote
    {3828, 1448},  // OpenCurlyQuote
    {3843, 1452},  // Or
    {3846, 1456},  // Oscr
    {3851, 1461},  // Oslash
    {3858, 1464},  // Otilde
    {3865, 1467},  // Otimes
    {3872, 1471},  // Ouml
    {3877, 1474},  // OverBar
    {3885, 1478},  // OverBrace
    {3895, 1482},  // OverBracket
    {3907, 1486},  // OverParenthesis
    {3923, 1490},  // PartialD
    {3932, 1494},  // Pcy
    {3936, 1497},  // Pfr
    {3940, 1502},  // Phi
    {3944, 1505},  // Pi
    {3947, 1508},  // PlusMinus
    {3957, 1511},  // Poincareplane
    {3971, 1515},  // Popf
    {3976, 1519},  // Pr
    {3979, 1523},  // Precedes
    {3988, 1527},  // PrecedesEqual
    {4002, 1531},  // PrecedesSlantEqual
    {4021, 1535},  // PrecedesTilde
    {4035, 1539},  // Prime
    {4041, 1543},  // Product
    {4049, 1547},  // Proportion
    {4060, 1551},  // Proportional
    {4073, 1555},  // Pscr
    {4078, 1560},  // Psi
    {4082, 1563},  // QUOT
    {4087, 1565},  // Qfr
    {4091, 1570},  // Qopf
    {4096, 1574},  // Qscr
    {4101, 1579},  // RBarr
    {4107, 1583},  // REG
    {4111, 1586},  // Racute
    {4118, 1589},  // Rang
    {4123, 1593},  // Rarr
    {4128, 1597},  // Rarrtl
    {4135, 1601},  // Rcaron
    {4142, 1604},  // Rcedil
    {4149, 1607},  // Rcy
    {4153, 1610},  // Re
    {4156, 1614},  // ReverseElement
    {4171, 1618},  // ReverseEquilibrium
    {4190, 1622},  // ReverseUpEquilibrium
    {4211, 1626},  // Rfr
    {4215, 1630},  // Rho
    {4219, 1633},  // RightAngleBracket
    {4237, 1637},  // RightArrow
    {4248, 1641},  // RightArrowBar
    {4262, 1645},  // RightArrowLeftArrow
    {4282, 1649},  // RightCeiling
    {4295, 1653},  // RightDoubleBracket
    {4314, 1657},  // RightDownTeeVector
    {4333, 1661},  // RightDownVector
    {4349, 1665},  // RightDownVectorBar
    {4368, 1669},  // RightFloor
    {4379, 1673},  // RightTee
    {4388, 1677},  // RightTeeArrow
    {4402, 1681},  // RightTeeVector
    {4417, 1685},  // RightTriangle
    {4431, 1689},  // RightTriangleBar
    {4448, 1693},  // RightTriangleEqual
    {4467, 1697},  // RightUpDownVector
    {4485, 1701},  // RightUpTeeVector
    {4502, 1705},  // RightUpVector
    {4516, 1709},  // RightUpVectorBar
    {4533, 1713},  // RightVector
    {4545, 1717},  // RightVectorBar
    {4560, 1721},  // Rightarrow
    {4571, 1725},  // Ropf
    {4576, 1729},  // RoundImplies
    {4589, 1733},  // Rrightarrow
    {4601, 1737},  // Rscr
    {4606, 1741},  // Rsh
    {4610, 1745},  // RuleDelayed
    {4622, 1749},  // SHCHcy
    {4629, 1752},  // SHcy
    {4634, 1755},  // SOFTcy
    {4641, 1758},  // Sacute
    {4648, 1761},  // Sc
    {4651, 1765},  // Scaron
    {4658, 1768},  // Scedil
    {4665, 1771},  // Scirc
    {4671, 1774},  // Scy
    {4675, 1777},  // Sfr
    {4679, 1782},  // ShortDownArrow
    {4694, 1786},  // ShortLeftArrow
    {4709, 1790},  // ShortRightArrow
    {4725, 1794},  // ShortUpArrow
    {4738, 1798},  // Sigma
    {4744, 1801},  // SmallCircle
    {4756, 1805},  // Sopf
    {4761, 1810},  // Sqrt
    {4766, 1814},  // Square
    {4773, 1818},  // SquareIntersection
    {4792, 1822},  // SquareSubset
    {4805, 1826},  // SquareSubsetEqual
    {4823, 1830},  // SquareSuperset
    {4838, 1834},  // SquareSupersetEqual
    {4858, 1838},  // SquareUnion
    {4870, 1842},  // Sscr
    {4875, 1847},  // Star
    {4880, 1851},  // Sub
    {4884, 1855},  // Subset
    {4891, 1859},  // SubsetEqual
    {4903, 1863},  // Succeeds
    {4912, 1867},  // SucceedsEqual
    {4926, 1871},  // SucceedsSlantEqual
    {4945, 1875},  // SucceedsTilde
    {4959, 1879},  // SuchThat
    {4968, 1883},  // Sum
    {4972, 1887},  // Sup
    {4976, 1891},  // Superset
    {4985, 1895},  // SupersetEqual
    {4999, 1899},  // Supset
    {5006, 1903},  // THORN
    {5012, 1906},  // TRADE
    {5018, 1910},  // TSHcy
    {5024, 1913},  // TScy
    {5029, 1916},  // Tab
    {5033, 1918},  // Tau
    {5037, 1921},  // Tcaron
    {5044, 1924},  // Tcedil
    {5051, 1927},  // Tcy
    {5055, 1930},  // Tfr
    {5059, 1935},  // Therefore
    {5069, 1939},  // Theta
    {5075, 1942},  // ThickSpace
    {5086, 1949},  // ThinSpace
    {5096, 1953},  // Tilde
    {5102, 1957},  // TildeEqual
    {5113, 1961},  // TildeFullEqual
    {5128, 1965},  // TildeTilde
    {5139, 1969},  // Topf
    {5144, 1974},  // TripleDot
    {5154, 1978},  // Tscr
    {5159, 1983},  // Tstrok
    {5166, 1986},  // Uacute
    {5173, 1989},  // Uarr
    {5178, 1993},  // Uarrocir
    {5187, 1997},  // Ubrcy
    {5193, 2000},  // Ubreve
    {5200, 2003},  // Ucirc
    {5206, 2006},  // Ucy
    {5210, 2009},  // Udblac
    {5217, 2012},  // Ufr
    {5221, 2017},  // Ugrave
    {5228, 2020},  // Umacr
    {5234, 2023},  // UnderBar
    {5243, 2025},  // UnderBrace
    {5254, 2029},  // UnderBracket
    {5267, 2033},  // UnderParenthesis
    {5284, 2037},  // Union
    {5290, 2041},  // UnionPlus
    {5300, 2045},  // Uogon
    {5306, 2048},  // Uopf
    {5311, 2053},  // UpArrow
    {5319, 2057},  // UpArrowBar
    {5330, 2061},  // UpArrowDownArrow
    {5347, 2065},  // UpDownArrow
    {5359, 2069},  // UpEquilibrium
    {5373, 2073},  // UpTee
    {5379, 2077},  // UpTeeArrow
    {5390, 2081},  // Uparrow
    {5398, 2085},  // Updownarrow
    {5410, 2089},  // UpperLeftArrow
    {5425, 2093},  // UpperRightArrow
    {5441, 2097},  // Upsi
    {5446, 2100},  // Upsilon
    {5454, 2103},  // Uring
    {5460, 2106},  // Uscr
    {5465, 2111},  // Utilde
    {5472, 2114},  // Uuml
    {5477, 2117},  // VDash
    {5483, 2121},  // Vbar
    {5488, 2125},  // Vcy
    {5492, 2128},  // Vdash
    {5498, 2132},  // Vdashl
    {5505, 2136},  // Vee
    {5509, 2140},  // Verbar
    {5516, 2144},  // Vert
    {5521, 2148},  // VerticalBar
    {5533, 2152},  // VerticalLine
    {5546, 2154},  // VerticalSeparator
    {5564, 2158},  // VerticalTilde
    {5578, 2162},  // VeryThinSpace
    {5592, 2166},  // Vfr
    {5596, 2171},  // Vopf
    {5601, 2176},  // Vscr
    {5606, 2181},  // Vvdash
    {5613, 2185},  // Wcirc
    {5619, 2188},  // Wedge
    {5625, 2192},  // Wfr
    {5629, 2197},  // Wopf
    {5634, 2202},  // Wscr
    {5639, 2207},  // Xfr
    {5643, 2212},  // Xi
    {5646, 2215},  // Xopf
    {5651, 2220},  // Xscr
    {5656, 2225},  // YAcy
    {5661, 2228},  // YIcy
    {5666, 2231},  // YUcy
    {5671, 2234},  // Yacute
    {5678, 2237},  // Ycirc
    {5684, 2240},  // Ycy
    {5688, 2243},  // Yfr
    {5692, 2248},  // Yopf
    {5697, 2253},  // Yscr
    {5702, 2258},  // Yuml
    {5707, 2261},  // ZHcy
    {5712, 2264},  // Zacute
    {5719, 2267},  // Zcaron
    {5726, 2270},  // Zcy
    {5730, 2273},  // Zdot
    {5735, 2276},  // ZeroWidthSpace
    {5750, 2280},  // Zeta
    {5755, 2283},  // Zfr
    {5759, 2287},  // Zopf
    {5764, 2291},  // Zscr
    {5769, 2296},  // aacute
    {5776, 2299},  // abreve
    {5783, 2302},  // ac
    {5786, 2306},  // acE
    {5790, 2312},  // acd
    {5794, 2316},  // acirc
    {5800, 2319},  // acute
    {5806, 2322},  // acy
    {5810, 2325},  // aelig
    {5816, 2328},  // af
    {5819, 2332},  // afr
    {5823, 2337},  // agrave
    {5830, 2340},  // alefsym
    {5838, 2344},  // aleph
    {5844, 2348},  // alpha
    {5850, 2351},  // amacr
    {5856, 2354},  // amalg
    {5862, 2358},  // amp
    {5866, 2360},  // and
    {5870, 2364},  // andand
    {5877, 2368},  // andd
    {5882, 2372},  // andslope
    {5891, 2376},  // andv
    {5896, 2380},  // ang
    {5900, 2384},  // ange
    {5905, 2388},  // angle
    {5911, 2392},  // angmsd
    {5918, 2396},  // angmsdaa
    {5927, 2400},  // angmsdab
    {5936, 2404},  // angmsdac
    {5945, 2408},  // angmsdad
    {5954, 2412},  // angmsdae
    {5963, 2416},  // angmsdaf
    {5972, 2420},  // angmsdag
    {5981, 2424},  // angmsdah
    {5990, 2428},  // angrt
    {5996, 2432},  // angrtvb
    {6004, 2436},  // angrtvbd
    {6013, 2440},  // angsph
    {6020, 2444},  // angst
    {6026, 2447},  // angzarr
    {6034, 2451},  // aogon
    {6040, 2454},  // aopf
    {6045, 2459},  // ap
    {6048, 2463},  // apE
    {6052, 2467},  // apacir
    {6059, 2471},  // ape
    {6063, 2475},  // apid
    {6068, 2479},  // apos
    {6073, 2481},  // approx
    {6080, 2485},  // approxeq
    {6089, 2489},  // aring
    {6095, 2492},  // ascr
    {6100, 2497},  // ast
    {6104, 2499},  // asymp
    {6110, 2503},  // asympeq
    {6118, 2507},  // atilde
    {6125, 2510},  // auml
    {6130, 2513},  // awconint
    {6139, 2517},  // awint
    {6145, 2521},  // bNot
    {6150, 2525},  // backcong
    {6159, 2529},  // backepsilon
    {6171, 2532},  // backprime
    {6181, 2536},  // backsim
    {6189, 2540},  // backsimeq
    {6199, 2544},  // barvee
    {6206, 2548},  // barwed
    {6213, 2552},  // barwedge
    {6222, 2556},  // bbrk
    {6227, 2560},  // bbrktbrk
    {6236, 2564},  // bcong
    {6242, 2568},  // bcy
    {6246, 2571},  // bdquo
    {6252, 2575},  // becaus
    {6259, 2579},  // because
    {6267, 2583},  // bemptyv
    {6275, 2587},  // bepsi
    {6281, 2590},  // bernou
    {6288, 2594},  // beta
    {6293, 2597},  // beth
    {6298, 2601},  // between
    {6306, 2605},  // bfr
    {6310, 2610},  // bigcap
    {6317, 2614},  // bigcirc
    {6325, 2618},  // bigcup
    {6332, 2622},  // bigodot
    {6340, 2626},  // bigoplus
    {6349, 2630},  // bigotimes
    {6359, 2634},  // bigsqcup
    {6368, 2638},  // bigstar
    {6376, 2642},  // bigtriangledown
    {6392, 2646},  // bigtriangleup
    {6406, 2650},  // biguplus
    {6415, 2654},  // bigvee
    {6422, 2658},  // bigwedge
    {6431, 2662},  // bkarow
    {6438, 2666},  // blacklozenge
    {6451, 2670},  // blacksquare
    {6463, 2674},  // blacktriangle
    {6477, 2678},  // blacktriangledown
    {6495, 2682},  // blacktriangleleft
    {6513, 2686},  // blacktriangleright
    {6532, 2690},  // blank
    {6538, 2694},  // blk12
    {6544, 2698},  // blk14
    {6550, 2702},  // blk34
    {6556, 2706},  // block
    {6562, 2710},  // bne
    {6566, 2715},  // bnequiv
    {6574, 2722},  // bnot
    {6579, 2726},  // bopf
    {6584, 2731},  // bot
    {6588, 2735},  // bottom
    {6595, 2739},  // bowtie
    {6602, 2743},  // boxDL
    {6608, 2747},  // boxDR
    {6614, 2751},  // boxDl
    {6620, 2755},  // boxDr
    {6626, 2759},  // boxH
    {6631, 2763},  // boxHD
    {6637, 2767},  // boxHU
    {6643, 2771},  // boxHd
    {6649, 2775},  // boxHu
    {6655, 2779},  // boxUL
    {6661, 2783},  // boxUR
    {6667, 2787},  // boxUl
    {6673, 2791},  // boxUr
    {6679, 2795},  // boxV
    {6684, 2799},  // boxVH
    {6690, 2803},  // boxVL
    {6696, 2807},  // boxVR
    {6702, 2811},  // boxVh
    {6708, 2815},  // boxVl
    {6714, 2819},  // boxVr
    {6720, 2823},  // boxbox
    {6727, 2827},  // boxdL
    {6733, 2831},  // boxdR
    {6739, 2835},  // boxdl
    {6745, 2839},  // boxdr
    {6751, 2843},  // boxh
    {6756, 2847},  // boxhD
    {6762, 2851},  // boxhU
    {6768, 2855},  // boxhd
    {6774, 2859},  // boxhu
    {6780, 2863},  // boxminus
    {6789, 2867},  // boxplus
    {6797, 2871},  // boxtimes
    {6806, 2875},  // boxuL
    {6812, 2879},  // boxuR
    {6818, 2883},  // boxul
    {6824, 2887},  // boxur
    {6830, 2891},  // boxv
    {6835, 2895},  // boxvH
    {6841, 2899},  // boxvL
    {6847, 2903},  // boxvR
    {6853, 2907},  // boxvh
    {6859, 2911},  // boxvl
    {6865, 2915},  // boxvr
    {6871, 2919},  // bprime
    {6878, 2923},  // breve
    {6884, 2926},  // brvbar
    {6891, 2929},  // bscr
    {6896, 2934},  // bsemi
    {6902, 2938},  // bsim
    {6907, 2942},  // bsime
    {6913, 2946},  // bsol
    {6918, 2948},  // bsolb
    {6924, 2952},  // bsolhsub
    {6933, 2956},  // bull
    {6938, 2960},  // bullet
    {6945, 2964},  // bump
    {6950, 2968},  // bumpE
    {6956, 2972},  // bumpe
    {6962, 2976},  // bumpeq
    {6969, 2980},  // cacute
    {6976, 2983},  // cap
    {6980, 2987},  // capand
    {6987, 2991},  // capbrcup
    {6996, 2995},  // capcap
    {7003, 2999},  // capcup
    {7010, 3003},  // capdot
    {7017, 3007},  // caps
    {7022, 3014},  // caret
    {7028, 3018},  // caron
    {7034, 3021},  // ccaps
    {7040, 3025},  // ccaron
    {7047, 3028},  // ccedil
    {7054, 3031},  // ccirc
    {7060, 3034},  // ccups
    {7066, 3038},  // ccupssm
    {7074, 3042},  // cdot
    {7079, 3045},  // cedil
    {7085, 3048},  // cemptyv
    {7093, 3052},  // cent
    {7098, 3055},  // centerdot
    {7108, 3058},  // cfr
    {7112, 3063},  // chcy
    {7117, 3066},  // check
    {7123, 3070},  // checkmark
    {7133, 3074},  // chi
    {7137, 3077},  // cir
    {7141, 3081},  // cirE
    {7146, 3085},  // circ
    {7151, 3088},  // circeq
    {7158, 3092},  // circlearrowleft
    {7174, 3096},  // circlearrowright
    {7191, 3100},  // circledR
    {7200, 3103},  // circledS
    {7209, 3107},  // circledast
    {7220, 3111},  // circledcirc
    {7232, 3115},  // circleddash
    {7244, 3119},  // cire
    {7249, 3123},  // cirfnint
    {7258, 3127},  // cirmid
    {7265, 3131},  // cirscir
    {7273, 3135},  // clubs
    {7279, 3139},  // clubsuit
    {7288, 3143},  // colon
    {7294, 3145},  // colone
    {7301, 3149},  // coloneq
    {7309, 3153},  // comma
    {7315, 3155},  // commat
    {7322, 3157},  // comp
    {7327, 3161},  // compfn
    {7334, 3165},  // complement
    {7345, 3169},  // complexes
    {7355, 3173},  // cong
    {7360, 3177},  // congdot
    {7368, 3181},  // conint
    {7375, 3185},  // copf
    {7380, 3190},  // coprod
    {7387, 3194},  // copy
    {7392, 3197},  // copysr
    {7399, 3201},  // crarr
    {7405, 3205},  // cross
    {7411, 3209},  // cscr
    {7416, 3214},  // csub
    {7421, 3218},  // csube
    {7427, 3222},  // csup
    {7432, 3226},  // csupe
    {7438, 3230},  // ctdot
    {7444, 3234},  // cudarrl
    {7452, 3238},  // cudarrr
    {7460, 3242},  // cuepr
    {7466, 3246},  // cuesc
    {7472, 3250},  // cularr
    {7479, 3254},  // cularrp
    {7487, 3258},  // cup
    {7491, 3262},  // cupbrcap
    {7500, 3266},  // cupcap
    {7507, 3270},  // cupcup
    {7514, 3274},  // cupdot
    {7521, 3278},  // cupor
    {7527, 3282},  // cups
    {7532, 3289},  // curarr
    {7539, 3293},  // curarrm
    {7547, 3297},  // curlyeqprec
    {7559, 3301},  // curlyeqsucc
    {7571, 3305},  // curlyvee
    {7580, 3309},  // curlywedge
    {7591, 3313},  // curren
    {7598, 3316},  // curvearrowleft
    {7613, 3320},  // curvearrowright
    {7629, 3324},  // cuvee
    {7635, 3328},  // cuwed
    {7641, 3332},  // cwconint
    {7650, 3336},  // cwint
    {7656, 3340},  // cylcty
    {7663, 3344},  // dArr
    {7668, 3348},  // dHar
    {7673, 3352},  // dagger
    {7680, 3356},  // daleth
    {7687, 3360},  // darr
    {7692, 3364},  // dash
    {7697, 3368},  // dashv
    {7703, 3372},  // dbkarow
    {7711, 3376},  // dblac
    {7717, 3379},  // dcaron
    {7724, 3382},  // dcy
    {7728, 3385},  // dd
    {7731, 3389},  // ddagger
    {7739, 3393},  // ddarr
    {7745, 3397},  // ddotseq
    {7753, 3401},  // deg
    {7757, 3404},  // delta
    {7763, 3407},  // demptyv
    {7771, 3411},  // dfisht
    {7778, 3415},  // dfr
    {7782, 3420},  // dharl
    {7788, 3424},  // dharr
    {7794, 3428},  // diam
    {7799, 3432},  // diamond
    {7807, 3436},  // diamondsuit
    {7819, 3440},  // diams
    {7825, 3444},  // die
    {7829, 3447},  // digamma
    {7837, 3450},  // disin
    {7843, 3454},  // div
    {7847, 3457},  // divide
    {7854, 3460},  // divideontimes
    {7868, 3464},  // divonx
    {7875, 3468},  // djcy
    {7880, 3471},  // dlcorn
    {7887, 3475},  // dlcrop
    {7894, 3479},  // dollar
    {7901, 3481},  // dopf
    {7906, 3486},  // dot
    {7910, 3489},  // doteq
    {7916, 3493},  // doteqdot
    {7925, 3497},  // dotminus
    {7934, 3501},  // dotplus
    {7942, 3505},  // dotsquare
    {7952, 3509},  // doublebarwedge
    {7967, 3513},  // downarrow
    {7977, 3517},  // downdownarrows
    {7992, 3521},  // downharpoonleft
    {8008, 3525},  // downharpoonright
    {8025, 3529},  // drbkarow
    {8034, 3533},  // drcorn
    {8041, 3537},  // drcrop
    {8048, 3541},  // dscr
    {8053, 3546},  // dscy
    {8058, 3549},  // dsol
    {8063, 3553},  // dstrok
    {8070, 3556},  // dtdot
    {8076, 3560},  // dtri
    {8081, 3564},  // dtrif
    {8087, 3568},  // duarr
    {8093, 3572},  // duhar
    {8099, 3576},  // dwangle
    {8107, 3580},  // dzcy
    {8112, 3583},  // dzigrarr
    {8121, 3587},  // eDDot
    {8127, 3591},  // eDot
    {8132, 3595},  // eacute
    {8139, 3598},  // easter
    {8146, 3602},  // ecaron
    {8153, 3605},  // ecir
    {8158, 3609},  // ecirc
    {8164, 3612},  // ecolon
    {8171, 3616},  // ecy
    {8175, 3619},  // edot
    {8180, 3622},  // ee
    {8183, 3626},  // efDot
    {8189, 3630},  // efr
    {8193, 3635},  // eg
    {8196, 3639},  // egrave
    {8203, 3642},  // egs
    {8207, 3646},  // egsdot
    {8214, 3650},  // el
    {8217, 3654},  // elinters
    {8226, 3658},  // ell
    {8230, 3662},  // els
    {8234, 3666},  // elsdot
    {8241, 3670},  // emacr
    {8247, 3673},  // empty
    {8253, 3677},  // emptyset
    {8262, 3681},  // emptyv
    {8269, 3685},  // emsp
    {8274, 3689},  // emsp13
    {8281, 3693},  // emsp14
    {8288, 3697},  // eng
    {8292, 3700},  // ensp
    {8297, 3704},  // eogon
    {8303, 3707},  // eopf
    {8308, 3712},  // epar
    {8313, 3716},  // eparsl
    {8320, 3720},  // eplus
    {8326, 3724},  // epsi
    {8331, 3727},  // epsilon
    {8339, 3730},  // epsiv
    {8345, 3733},  // eqcirc
    {8352, 3737},  // eqcolon
    {8360, 3741},  // eqsim
    {8366, 3745},  // eqslantgtr
    {8377, 3749},  // eqslantless
    {8389, 3753},  // equals
    {8396, 3755},  // equest
    {8403, 3759},  // equiv
    {8409, 3763},  // equivDD
    {8417, 3767},  // eqvparsl
    {8426, 3771},  // erDot
    {8432, 3775},  // erarr
    {8438, 3779},  // escr
    {8443, 3783},  // esdot
    {8449, 3787},  // esim
    {8454, 3791},  // eta
    {8458, 3794},  // eth
    {8462, 3797},  // euml
    {8467, 3800},  // euro
    {8472, 3804},  // excl
    {8477, 3806},  // exist
    {8483, 3810},  // expectation
    {8495, 3814},  // exponentiale
    {8508, 3818},  // fallingdotseq
    {8522, 3822},  // fcy
    {8526, 3825},  // female
    {8533, 3829},  // ffilig
    {8540, 3833},  // fflig
    {8546, 3837},  // ffllig
    {8553, 3841},  // ffr
    {8557, 3846},  // filig
    {8563, 3850},  // fjlig
    {8569, 3853},  // flat
    {8574, 3857},  // fllig
    {8580, 3861},  // fltns
    {8586, 3865},  // fnof
    {8591, 3868},  // fopf
    {8596, 3873},  // forall
    {8603, 3877},  // fork
    {8608, 3881},  // forkv
    {8614, 3885},  // fpartint
    {8623, 3889},  // frac12
    {8630, 3892},  // frac13
    {8637, 3896},  // frac14
    {8644, 3899},  // frac15
    {8651, 3903},  // frac16
    {8658, 3907},  // frac18
    {8665, 3911},  // frac23
    {8672, 3915},  // frac25
    {8679, 3919},  // frac34
    {8686, 3922},  // frac35
    {8693, 3926},  // frac38
    {8700, 3930},  // frac45
    {8707, 3934},  // frac56
    {8714, 3938},  // frac58
    {8721, 3942},  // frac78
    {8728, 3946},  // frasl
    {8734, 3950},  // frown
    {8740, 3954},  // fscr
    {8745, 3959},  // gE
    {8748, 3963},  // gEl
    {8752, 3967},  // gacute
    {8759, 3970},  // gamma
    {8765, 3973},  // gammad
    {8772, 3976},  // gap
    {8776, 3980},  // gbreve
    {8783, 3983},  // gcirc
    {8789, 3986},  // gcy
    {8793, 3989},  // gdot
    {8798, 3992},  // ge
    {8801, 3996},  // gel
    {8805, 4000},  // geq
    {8809, 4004},  // geqq
    {8814, 4008},  // geqslant
    {8823, 4012},  // ges
    {8827, 4016},  // gescc
    {8833, 4020},  // gesdot
    {8840, 4024},  // gesdoto
    {8848, 4028},  // gesdotol
    {8857, 4032},  // gesl
    {8862, 4039},  // gesles
    {8869, 4043},  // gfr
    {8873, 4048},  // gg
    {8876, 4052},  // ggg
    {8880, 4056},  // gimel
    {8886, 4060},  // gjcy
    {8891, 4063},  // gl
    {8894, 4067},  // glE
    {8898, 4071},  // gla
    {8902, 4075},  // glj
    {8906, 4079},  // gnE
    {8910, 4083},  // gnap
    {8915, 4087},  // gnapprox
    {8924, 4091},  // gne
    {8928, 4095},  // gneq
    {8933, 4099},  // gneqq
    {8939, 4103},  // gnsim
    {8945, 4107},  // gopf
    {8950, 4112},  // grave
    {8956, 4114},  // gscr
    {8961, 4118},  // gsim
    {8966, 4122},  // gsime
    {8972, 4126},  // gsiml
    {8978, 4130},  // gt
    {8981, 4132},  // gtcc
    {8986, 4136},  // gtcir
    {8992, 4140},  // gtdot
    {8998, 4144},  // gtlPar
    {9005, 4148},  // gtquest
    {9013, 4152},  // gtrapprox
    {9023, 4156},  // gtrarr
    {9030, 4160},  // gtrdot
    {9037, 4164},  // gtreqless
    {9047, 4168},  // gtreqqless
    {9058, 4172},  // gtrless
    {9066, 4176},  // gtrsim
    {9073, 4180},  // gvertneqq
    {9083, 4187},  // gvnE
    {9088, 4194},  // hArr
    {9093, 4198},  // hairsp
    {9100, 4202},  // half
    {9105, 4205},  // hamilt
    {9112, 4209},  // hardcy
    {9119, 4212},  // harr
    {9124, 4216},  // harrcir
    {9132, 4220},  // harrw
    {9138, 4224},  // hbar
    {9143, 4228},  // hcirc
    {9149, 4231},  // hearts
    {9156, 4235},  // heartsuit
    {9166, 4239},  // hellip
    {9173, 4243},  // hercon
    {9180, 4247},  // hfr
    {9184, 4252},  // hksearow
    {9193, 4256},  // hkswarow
    {9202, 4260},  // hoarr
    {9208, 4264},  // homtht
    {9215, 4268},  // hookleftarrow
    {9229, 4272},  // hookrightarrow
    {9244, 4276},  // hopf
    {9249, 4281},  // horbar
    {9256, 4285},  // hscr
    {9261, 4290},  // hslash
    {9268, 4294},  // hstrok
    {9275, 4297},  // hybull
    {9282, 4301},  // hyphen
    {9289, 4305},  // iacute
    {9296, 4308},  // ic
    {9299, 4312},  // icirc
    {9305, 4315},  // icy
    {9309, 4318},  // iecy
    {9314, 4321},  // iexcl
    {9320, 4324},  // iff
    {9324, 4328},  // ifr
    {9328, 4333},  // igrave
    {9335, 4336},  // ii
    {9338, 4340},  // iiiint
    {9345, 4344},  // iiint
    {9351, 4348},  // iinfin
    {9358, 4352},  // iiota
    {9364, 4356},  // ijlig
    {9370, 4359},  // imacr
    {9376, 4362},  // image
    {9382, 4366},  // imagline
    {9391, 4370},  // imagpart
    {9400, 4374},  // imath
    {9406, 4377},  // imof
    {9411, 4381},  // imped
    {9417, 4384},  // in
    {9420, 4388},  // incare
    {9427, 4392},  // infin
    {9433, 4396},  // infintie
    {9442, 4400},  // inodot
    {9449, 4403},  // int
    {9453, 4407},  // intcal
    {9460, 4411},  // integers
    {9469, 4415},  // intercal
    {9478, 4419},  // intlarhk
    {9487, 4423},  // intprod
    {9495, 4427},  // iocy
    {9500, 4430},  // iogon
    {9506, 4433},  // iopf
    {9511, 4438},  // iota
    {9516, 4441},  // iprod
    {9522, 4445},  // iquest
    {9529, 4448},  // iscr
    {9534, 4453},  // isin
    {9539, 4457},  // isinE
    {9545, 4461},  // isindot
    {9553, 4465},  // isins
    {9559, 4469},  // isinsv
    {9566, 4473},  // isinv
    {9572, 4477},  // it
    {9575, 4481},  // itilde
    {9582, 4484},  // iukcy
    {9588, 4487},  // iuml
    {9593, 4490},  // jcirc
    {9599, 4493},  // jcy
    {9603, 4496},  // jfr
    {9607, 4501},  // jmath
    {9613, 4504},  // jopf
    {9618, 4509},  // jscr
    {9623, 4514},  // jsercy
    {9630, 4517},  // jukcy
    {9636, 4520},  // kappa
    {9642, 4523},  // kappav
    {9649, 4526},  // kcedil
    {9656, 4529},  // kcy
    {9660, 4532},  // kfr
    {9664, 4537},  // kgreen
    {9671, 4540},  // khcy
    {9676, 4543},  // kjcy
    {9681, 4546},  // kopf
    {9686, 4551},  // kscr
    {9691, 4556},  // lAarr
    {9697, 4560},  // lArr
    {9702, 4564},  // lAtail
    {9709, 4568},  // lBarr
    {9715, 4572},  // lE
    {9718, 4576},  // lEg
    {9722, 4580},  // lHar
    {9727, 4584},  // lacute
    {9734, 4587},  // laemptyv
    {9743, 4591},  // lagran
    {9750, 4595},  // lambda
    {9757, 4598},  // lang
    {9762, 4602},  // langd
    {9768, 4606},  // langle
    {9775, 4610},  // lap
    {9779, 4614},  // laquo
    {9785, 4617},  // larr
    {9790, 4621},  // larrb
    {9796, 4625},  // larrbfs
    {9804, 4629},  // larrfs
    {9811, 4633},  // larrhk
    {9818, 4637},  // larrlp
    {9825, 4641},  // larrpl
    {9832, 4645},  // larrsim
    {9840, 4649},  // larrtl
    {9847, 4653},  // lat
    {9851, 4657},  // latail
    {9858, 4661},  // late
    {9863, 4665},  // lates
    {9869, 4672},  // lbarr
    {9875, 4676},  // lbbrk
    {9881, 4680},  // lbrace
    {9888, 4682},  // lbrack
    {9895, 4684},  // lbrke
    {9901, 4688},  // lbrksld
    {9909, 4692},  // lbrkslu
    {9917, 4696},  // lcaron
    {9924, 4699},  // lcedil
    {9931, 4702},  // lceil
    {9937, 4706},  // lcub
    {9942, 4708},  // lcy
    {9946, 4711},  // ldca
    {9951, 4715},  // ldquo
    {9957, 4719},  // ldquor
    {9964, 4723},  // ldrdhar
    {9972, 4727},  // ldrushar
    {9981, 4731},  // ldsh
    {9986, 4735},  // le
    {9989, 4739},  // leftarrow
    {9999, 4743},  // leftarrowtail
    {10013, 4747}, // leftharpoondown
    {10029, 4751}, // leftharpoonup
    {10043, 4755}, // leftleftarrows
    {10058, 4759}, // leftrightarrow
    {10073, 4763}, // leftrightarrows
    {10089, 4767}, // leftrightharpoons
    {10107, 4771}, // leftrightsquigarrow
    {10127, 4775}, // leftthreetimes
    {10142, 4779}, // leg
    {10146, 4783}, // leq
    {10150, 4787}, // leqq
    {10155, 4791}, // leqslant
    {10164, 4795}, // les
    {10168, 4799}, // lescc
    {10174, 4803}, // lesdot
    {10181, 4807}, // lesdoto
    {10189, 4811}, // lesdotor
    {10198, 4815}, // lesg
    {10203, 4822}, // lesges
    {10210, 4826}, // lessapprox
    {10221, 4830}, // lessdot
    {10229, 4834}, // lesseqgtr
    {10239, 4838}, // lesseqqgtr
    {10250, 4842}, // lessgtr
    {10258, 4846}, // lesssim
    {10266, 4850}, // lfisht
    {10273, 4854}, // lfloor
    {10280, 4858}, // lfr
    {10284, 4863}, // lg
    {10287, 4867}, // lgE
    {10291, 4871}, // lhard
    {10297, 4875}, // lharu
    {10303, 4879}, // lharul
    {10310, 4883}, // lhblk
    {10316, 4887}, // ljcy
    {10321, 4890}, // ll
    {10324, 4894}, // llarr
    {10330, 4898}, // llcorner
    {10339, 4902}, // llhard
    {10346, 4906}, // lltri
    {10352, 4910}, // lmidot
    {10359, 4913}, // lmoust
    {10366, 4917}, // lmoustache
    {10377, 4921}, // lnE
    {10381, 4925}, // lnap
    {10386, 4929}, // lnapprox
    {10395, 4933}, // lne
    {10399, 4937}, // lneq
    {10404, 4941}, // lneqq
    {10410, 4945}, // lnsim
    {10416, 4949}, // loang
    {10422, 4953}, // loarr
    {10428, 4957}, // lobrk
    {10434, 4961}, // longleftarrow
    {10448, 4965}, // longleftrightarrow
    {10467, 4969}, // longmapsto
    {10478, 4973}, // longrightarrow
    {10493, 4977}, // looparrowleft
    {10507, 4981}, // looparrowright
    {10522, 4985}, // lopar
    {10528, 4989}, // lopf
    {10533, 4994}, // loplus
    {10540, 4998}, // lotimes
    {10548, 5002}, // lowast
    {10555, 5006}, // lowbar
    {10562, 5008}, // loz
    {10566, 5012}, // lozenge
    {10574, 5016}, // lozf
    {10579, 5020}, // lpar
    {10584, 5022}, // lparlt
    {10591, 5026}, // lrarr
    {10597, 5030}, // lrcorner
    {10606, 5034}, // lrhar
    {10612, 5038}, // lrhard
    {10619, 5042}, // lrm
    {10623, 5046}, // lrtri
    {10629, 5050}, // lsaquo
    {10636, 5054}, // lscr
    {10641, 5059}, // lsh
    {10645, 5063}, // lsim
    {10650, 5067}, // lsime
    {10656, 5071}, // lsimg
    {10662, 5075}, // lsqb
    {10667, 5077}, // lsquo
    {10673, 5081}, // lsquor
    {10680, 5085}, // lstrok
    {10687, 5088}, // lt
    {10690, 5090}, // ltcc
    {10695, 5094}, // ltcir
    {10701, 5098}, // ltdot
    {10707, 5102}, // lthree
    {10714, 5106}, // ltimes
    {10721, 5110}, // ltlarr
    {10728, 5114}, // ltquest
    {10736, 5118}, // ltrPar
    {10743, 5122}, // ltri
    {10748, 5126}, // ltrie
    {10754, 5130}, // ltrif
    {10760, 5134}, // lurdshar
    {10769, 5138}, // luruhar
    {10777, 5142}, // lvertneqq
    {10787, 5149}, // lvnE
    {10792, 5156}, // mDDot
    {10798, 5160}, // macr
    {10803, 5163}, // male
    {10808, 5167}, // malt
    {10813, 5171}, // maltese
    {10821, 5175}, // map
    {10825, 5179}, // mapsto
    {10832, 5183}, // mapstodown
    {10843, 5187}, // mapstoleft
    {10854, 5191}, // mapstoup
    {10863, 5195}, // marker
    {10870, 5199}, // mcomma
    {10877, 5203}, // mcy
    {10881, 5206}, // mdash
    {10887, 5210}, // measuredangle
    {10901, 5214}, // mfr
    {10905, 5219}, // mho
    {10909, 5223}, // micro
    {10915, 5226}, // mid
    {10919, 5230}, // midast
    {10926, 5232}, // midcir
    {10933, 5236}, // middot
    {10940, 5239}, // minus
    {10946, 5243}, // minusb
    {10953, 5247}, // minusd
    {10960, 5251}, // minusdu
    {10968, 5255}, // mlcp
    {10973, 5259}, // mldr
    {10978, 5263}, // mnplus
    {10985, 5267}, // models
    {10992, 5271}, // mopf
    {10997, 5276}, // mp
    {11000, 5280}, // mscr
    {11005, 5285}, // mstpos
    {11012, 5289}, // mu
    {11015, 5292}, // multimap
    {11024, 5296}, // mumap
    {11030, 5300}, // nGg
    {11034, 5306}, // nGt
    {11038, 5313}, // nGtv
    {11043, 5319}, // nLeftarrow
    {11054, 5323}, // nLeftrightarrow
    {11070, 5327}, // nLl
    {11074, 5333}, // nLt
    {11078, 5340}, // nLtv
    {11083, 5346}, // nRightarrow
    {11095, 5350}, // nVDash
    {11102, 5354}, // nVdash
    {11109, 5358}, // nabla
    {11115, 5362}, // nacute
    {11122, 5365}, // nang
    {11127, 5372}, // nap
    {11131, 5376}, // napE
    {11136, 5382}, // napid
    {11142, 5388}, // napos
    {11148, 5391}, // napprox
    {11156, 5395}, // natur
    {11162, 5399}, // natural
    {11170, 5403}, // naturals
    {11179, 5407}, // nbsp
    {11184, 5410}, // nbump
    {11190, 5416}, // nbumpe
    {11197, 5422}, // ncap
    {11202, 5426}, // ncaron
    {11209, 5429}, // ncedil
    {11216, 5432}, // ncong
    {11222, 5436}, // ncongdot
    {11231, 5442}, // ncup
    {11236, 5446}, // ncy
    {11240, 5449}, // ndash
    {11246, 5453}, // ne
    {11249, 5457}, // neArr
    {11255, 5461}, // nearhk
    {11262, 5465}, // nearr
    {11268, 5469}, // nearrow
    {11276, 5473}, // nedot
    {11282, 5479}, // nequiv
    {11289, 5483}, // nesear
    {11296, 5487}, // nesim
    {11302, 5493}, // nexist
    {11309, 5497}, // nexists
    {11317, 5501}, // nfr
    {11321, 5506}, // ngE
    {11325, 5512}, // nge
    {11329, 5516}, // ngeq
    {11334, 5520}, // ngeqq
    {11340, 5526}, // ngeqslant
    {11350, 5532}, // nges
    {11355, 5538}, // ngsim
    {11361, 5542}, // ngt
    {11365, 5546}, // ngtr
    {11370, 5550}, // nhArr
    {11376, 5554}, // nharr
    {11382, 5558}, // nhpar
    {11388, 5562}, // ni
    {11391, 5566}, // nis
    {11395, 5570}, // nisd
    {11400, 5574}, // niv
    {11404, 5578}, // njcy
    {11409, 5581}, // nlArr
    {11415, 5585}, // nlE
    {11419, 5591}, // nlarr
    {11425, 5595}, // nldr
    {11430, 5599}, // nle
    {11434, 5603}, // nleftarrow
    {11445, 5607}, // nleftrightarrow
    {11461, 5611}, // nleq
    {11466, 5615}, // nleqq
    {11472, 5621}, // nleqslant
    {11482, 5627}, // nles
    {11487, 5633}, // nless
    {11493, 5637}, // nlsim
    {11499, 5641}, // nlt
    {11503, 5645}, // nltri
    {11509, 5649}, // nltrie
    {11516, 5653}, // nmid
    {11521, 5657}, // nopf
    {11526, 5662}, // not
    {11530, 5665}, // notin
    {11536, 5669}, // notinE
    {11543, 5675}, // notindot
    {11552, 5681}, // notinva
    {11560, 5685}, // notinvb
    {11568, 5689}, // notinvc
    {11576, 5693}, // notni
    {11582, 5697}, // notniva
    {11590, 5701}, // notnivb
    {11598, 5705}, // notnivc
    {11606, 5709}, // npar
    {11611, 5713}, // nparallel
    {11621, 5717}, // nparsl
    {11628, 5724}, // npart
    {11634, 5730}, // npolint
    {11642, 5734}, // npr
    {11646, 5738}, // nprcue
    {11653, 5742}, // npre
    {11658, 5748}, // nprec
    {11664, 5752}, // npreceq
    {11672, 5758}, // nrArr
    {11678, 5762}, // nrarr
    {11684, 5766}, // nrarrc
    {11691, 5772}, // nrarrw
    {11698, 5778}, // nrightarrow
    {11710, 5782}, // nrtri
    {11716, 5786}, // nrtrie
    {11723, 5790}, // nsc
    {11727, 5794}, // nsccue
    {11734, 5798}, // nsce
    {11739, 5804}, // nscr
    {11744, 5809}, // nshortmid
    {11754, 5813}, // nshortparallel
    {11769, 5817}, // nsim
    {11774, 5821}, // nsime
    {11780, 5825}, // nsimeq
    {11787, 5829}, // nsmid
    {11793, 5833}, // nspar
    {11799, 5837}, // nsqsube
    {11807, 5841}, // nsqsupe
    {11815, 5845}, // nsub
    {11820, 5849}, // nsubE
    {11826, 5855}, // nsube
    {11832, 5859}, // nsubset
    {11840, 5866}, // nsubseteq
    {11850, 5870}, // nsubseteqq
    {11861, 5876}, // nsucc
    {11867, 5880}, // nsucceq
    {11875, 5886}, // nsup
    {11880, 5890}, // nsupE
    {11886, 5896}, // nsupe
    {11892, 5900}, // nsupset
    {11900, 5907}, // nsupseteq
    {11910, 5911}, // nsupseteqq
    {11921, 5917}, // ntgl
    {11926, 5921}, // ntilde
    {11933, 5924}, // ntlg
    {11938, 5928}, // ntriangleleft
    {11952, 5932}, // ntrianglelefteq
    {11968, 5936}, // ntriangleright
    {11983, 5940}, // ntrianglerighteq
    {12000, 5944}, // nu
    {12003, 5947}, // num
    {12007, 5949}, // numero
    {12014, 5953}, // numsp
    {12020, 5957}, // nvDash
    {12027, 5961}, // nvHarr
    {12034, 5965}, // nvap
    {12039, 5972}, // nvdash
    {12046, 5976}, // nvge
    {12051, 5983}, // nvgt
    {12056, 5988}, // nvinfin
    {12064, 5992}, // nvlArr
    {12071, 5996}, // nvle
    {12076, 6003}, // nvlt
    {12081, 6008}, // nvltrie
    {12089, 6015}, // nvrArr
    {12096, 6019}, // nvrtrie
    {12104, 6026}, // nvsim
    {12110, 6033}, // nwArr
    {12116, 6037}, // nwarhk
    {12123, 6041}, // nwarr
    {12129, 6045}, // nwarrow
    {12137, 6049}, // nwnear
    {12144, 6053}, // oS
    {12147, 6057}, // oacute
    {12154, 6060}, // oast
    {12159, 6064}, // ocir
    {12164, 6068}, // ocirc
    {12170, 6071}, // ocy
    {12174, 6074}, // odash
    {12180, 6078}, // odblac
    {12187, 6081}, // odiv
    {12192, 6085}, // odot
    {12197, 6089}, // odsold
    {12204, 6093}, // oelig
    {12210, 6096}, // ofcir
    {12216, 6100}, // ofr
    {12220, 6105}, // ogon
    {12225, 6108}, // ograve
    {12232, 6111}, // ogt
    {12236, 6115}, // ohbar
    {12242, 6119}, // ohm
    {12246, 6122}, // oint
    {12251, 6126}, // olarr
    {12257, 6130}, // olcir
    {12263, 6134}, // olcross
    {12271, 6138}, // oline
    {12277, 6142}, // olt
    {12281, 6146}, // omacr
    {12287, 6149}, // omega
    {12293, 6152}, // omicron
    {12301, 6155}, // omid
    {12306, 6159}, // ominus
    {12313, 6163}, // oopf
    {12318, 6168}, // opar
    {12323, 6172}, // operp
    {12329, 6176}, // oplus
    {12335, 6180}, // or
    {12338, 6184}, // orarr
    {12344, 6188}, // ord
    {12348, 6192}, // order
    {12354, 6196}, // orderof
    {12362, 6200}, // ordf
    {12367, 6203}, // ordm
    {12372, 6206}, // origof
    {12379, 6210}, // oror
    {12384, 6214}, // orslope
    {12392, 6218}, // orv
    {12396, 6222}, // oscr
    {12401, 6226}, // oslash
    {12408, 6229}, // osol
    {12413, 6233}, // otilde
    {12420, 6236}, // otimes
    {12427, 6240}, // otimesas
    {12436, 6244}, // ouml
    {12441, 6247}, // ovbar
    {12447, 6251}, // par
    {12451, 6255}, // para
    {12456, 6258}, // parallel
    {12465, 6262}, // parsim
    {12472, 6266}, // parsl
    {12478, 6270}, // part
    {12483, 6274}, // pcy
    {12487, 6277}, // percnt
    {12494, 6279}, // period
    {12501, 6281}, // permil
    {12508, 6285}, // perp
    {12513, 6289}, // pertenk
    {12521, 6293}, // pfr
    {12525, 6298}, // phi
    {12529, 6301}, // phiv
    {12534, 6304}, // phmmat
    {12541, 6308}, // phone
    {12547, 6312}, // pi
    {12550, 6315}, // pitchfork
    {12560, 6319}, // piv
    {12564, 6322}, // planck
    {12571, 6326}, // planckh
    {12579, 6330}, // plankv
    {12586, 6334}, // plus
    {12591, 6336}, // plusacir
    {12600, 6340}, // plusb
    {12606, 6344}, // pluscir
    {12614, 6348}, // plusdo
    {12621, 6352}, // plusdu
    {12628, 6356}, // pluse
    {12634, 6360}, // plusmn
    {12641, 6363}, // plussim
    {12649, 6367}, // plustwo
    {12657, 6371}, // pm
    {12660, 6374}, // pointint
    {12669, 6378}, // popf
    {12674, 6383}, // pound
    {12680, 6386}, // pr
    {12683, 6390}, // prE
    {12687, 6394}, // prap
    {12692, 6398}, // prcue
    {12698, 6402}, // pre
    {12702, 6406}, // prec
    {12707, 6410}, // precapprox
    {12718, 6414}, // preccurlyeq
    {12730, 6418}, // preceq
    {12737, 6422}, // precnapprox
    {12749, 6426}, // precneqq
    {12758, 6430}, // precnsim
    {12767, 6434}, // precsim
    {12775, 6438}, // prime
    {12781, 6442}, // primes
    {12788, 6446}, // prnE
    {12793, 6450}, // prnap
    {12799, 6454}, // prnsim
    {12806, 6458}, // prod
    {12811, 6462}, // profalar
    {12820, 6466}, // profline
    {12829, 6470}, // profsurf
    {12838, 6474}, // prop
    {12843, 6478}, // propto
    {12850, 6482}, // prsim
    {12856, 6486}, // prurel
    {12863, 6490}, // pscr
    {12868, 6495}, // psi
    {12872, 6498}, // puncsp
    {12879, 6502}, // qfr
    {12883, 6507}, // qint
    {12888, 6511}, // qopf
    {12893, 6516}, // qprime
    {12900, 6520}, // qscr
    {12905, 6525}, // quaternions
    {12917, 6529}, // quatint
    {12925, 6533}, // quest
    {12931, 6535}, // questeq
    {12939, 6539}, // quot
    {12944, 6541}, // rAarr
    {12950, 6545}, // rArr
    {12955, 6549}, // rAtail
    {12962, 6553}, // rBarr
    {12968, 6557}, // rHar
    {12973, 6561}, // race
    {12978, 6567}, // racute
    {12985, 6570}, // radic
    {12991, 6574}, // raemptyv
    {13000, 6578}, // rang
    {13005, 6582}, // rangd
    {13011, 6586}, // range
    {13017, 6590}, // rangle
    {13024, 6594}, // raquo
    {13030, 6597}, // rarr
    {13035, 6601}, // rarrap
    {13042, 6605}, // rarrb
    {13048, 6609}, // rarrbfs
    {13056, 6613}, // rarrc
    {13062, 6617}, // rarrfs
    {13069, 6621}, // rarrhk
    {13076, 6625}, // rarrlp
    {13083, 6629}, // rarrpl
    {13090, 6633}, // rarrsim
    {13098, 6637}, // rarrtl
    {13105, 6641}, // rarrw
    {13111, 6645}, // ratail
    {13118, 6649}, // ratio
    {13124, 6653}, // rationals
    {13134, 6657}, // rbarr
    {13140, 6661}, // rbbrk
    {13146, 6665}, // rbrace
    {13153, 6667}, // rbrack
    {13160, 6669}, // rbrke
    {13166, 6673}, // rbrksld
    {13174, 6677}, // rbrkslu
    {13182, 6681}, // rcaron
    {13189, 6684}, // rcedil
    {13196, 6687}, // rceil
    {13202, 6691}, // rcub
    {13207, 6693}, // rcy
    {13211, 6696}, // rdca
    {13216, 6700}, // rdldhar
    {13224, 6704}, // rdquo
    {13230, 6708}, // rdquor
    {13237, 6712}, // rdsh
    {13242, 6716}, // real
    {13247, 6720}, // realine
    {13255, 6724}, // realpart
    {13264, 6728}, // reals
    {13270, 6732}, // rect
    {13275, 6736}, // reg
    {13279, 6739}, // rfisht
    {13286, 6743}, // rfloor
    {13293, 6747}, // rfr
    {13297, 6752}, // rhard
    {13303, 6756}, // rharu
    {13309, 6760}, // rharul
    {13316, 6764}, // rho
    {13320, 6767}, // rhov
    {13325, 6770}, // rightarrow
    {13336, 6774}, // rightarrowtail
    {13351, 6778}, // rightharpoondown
    {13368, 6782}, // rightharpoonup
    {13383, 6786}, // rightleftarrows
    {13399, 6790}, // rightleftharpoons
    {13417, 6794}, // rightrightarrows
    {13434, 6798}, // rightsquigarrow
    {13450, 6802}, // rightthreetimes
    {13466, 6806}, // ring
    {13471, 6809}, // risingdotseq
    {13484, 6813}, // rlarr
    {13490, 6817}, // rlhar
    {13496, 6821}, // rlm
    {13500, 6825}, // rmoust
    {13507, 6829}, // rmoustache
    {13518, 6833}, // rnmid
    {13524, 6837}, // roang
    {13530, 6841}, // roarr
    {13536, 6845}, // robrk
    {13542, 6849}, // ropar
    {13548, 6853}, // ropf
    {13553, 6858}, // roplus
    {13560, 6862}, // rotimes
    {13568, 6866}, // rpar
    {13573, 6868}, // rpargt
    {13580, 6872}, // rppolint
    {13589, 6876}, // rrarr
    {13595, 6880}, // rsaquo
    {13602, 6884}, // rscr
    {13607, 6889}, // rsh
    {13611, 6893}, // rsqb
    {13616, 6895}, // rsquo
    {13622, 6899}, // rsquor
    {13629, 6903}, // rthree
    {13636, 6907}, // rtimes
    {13643, 6911}, // rtri
    {13648, 6915}, // rtrie
    {13654, 6919}, // rtrif
    {13660, 6923}, // rtriltri
    {13669, 6927}, // ruluhar
    {13677, 6931}, // rx
    {13680, 6935}, // sacute
    {13687, 6938}, // sbquo
    {13693, 6942}, // sc
    {13696, 6946}, // scE
    {13700, 6950}, // scap
    {13705, 6954}, // scaron
    {13712, 6957}, // sccue
    {13718, 6961}, // sce
    {13722, 6965}, // scedil
    {13729, 6968}, // scirc
    {13735, 6971}, // scnE
    {13740, 6975}, // scnap
    {13746, 6979}, // scnsim
    {13753, 6983}, // scpolint
    {13762, 6987}, // scsim
    {13768, 6991}, // scy
    {13772, 6994}, // sdot
    {13777, 6998}, // sdotb
    {13783, 7002}, // sdote
    {13789, 7006}, // seArr
    {13795, 7010}, // searhk
    {13802, 7014}, // searr
    {13808, 7018}, // searrow
    {13816, 7022}, // sect
    {13821, 7025}, // semi
    {13826, 7027}, // seswar
    {13833, 7031}, // setminus
    {13842, 7035}, // setmn
    {13848, 7039}, // sext
    {13853, 7043}, // sfr
    {13857, 7048}, // sfrown
    {13864, 7052}, // sharp
    {13870, 7056}, // shchcy
    {13877, 7059}, // shcy
    {13882, 7062}, // shortmid
    {13891, 7066}, // shortparallel
    {13905, 7070}, // shy
    {13909, 7073}, // sigma
    {13915, 7076}, // sigmaf
    {13922, 7079}, // sigmav
    {13929, 7082}, // sim
    {13933, 7086}, // simdot
    {13940, 7090}, // sime
    {13945, 7094}, // simeq
    {13951, 7098}, // simg
    {13956, 7102}, // simgE
    {13962, 7106}, // siml
    {13967, 7110}, // simlE
    {13973, 7114}, // simne
    {13979, 7118}, // simplus
    {13987, 7122}, // simrarr
    {13995, 7126}, // slarr
    {14001, 7130}, // smallsetminus
    {14015, 7134}, // smashp
    {14022, 7138}, // smeparsl
    {14031, 7142}, // smid
    {14036, 7146}, // smile
    {14042, 7150}, // smt
    {14046, 7154}, // smte
    {14051, 7158}, // smtes
    {14057, 7165}, // softcy
    {14064, 7168}, // sol
    {14068, 7170}, // solb
    {14073, 7174}, // solbar
    {14080, 7178}, // sopf
    {14085, 7183}, // spades
    {14092, 7187}, // spadesuit
    {14102, 7191}, // spar
    {14107, 7195}, // sqcap
    {14113, 7199}, // sqcaps
    {14120, 7206}, // sqcup
    {14126, 7210}, // sqcups
    {14133, 7217}, // sqsub
    {14139, 7221}, // sqsube
    {14146, 7225}, // sqsubset
    {14155, 7229}, // sqsubseteq
    {14166, 7233}, // sqsup
    {14172, 7237}, // sqsupe
    {14179, 7241}, // sqsupset
    {14188, 7245}, // sqsupseteq
    {14199, 7249}, // squ
    {14203, 7253}, // square
    {14210, 7257}, // squarf
    {14217, 7261}, // squf
    {14222, 7265}, // srarr
    {14228, 7269}, // sscr
    {14233, 7274}, // ssetmn
    {14240, 7278}, // ssmile
    {14247, 7282}, // sstarf
    {14254, 7286}, // star
    {14259, 7290}, // starf
    {14265, 7294}, // straightepsilon
    {14281, 7297}, // straightphi
    {14293, 7300}, // strns
    {14299, 7303}, // sub
    {14303, 7307}, // subE
    {14308, 7311}, // subdot
    {14315, 7315}, // sube
    {14320, 7319}, // subedot
    {14328, 7323}, // submult
    {14336, 7327}, // subnE
    {14342, 7331}, // subne
    {14348, 7335}, // subplus
    {14356, 7339}, // subrarr
    {14364, 7343}, // subset
    {14371, 7347}, // subseteq
    {14380, 7351}, // subseteqq
    {14390, 7355}, // subsetneq
    {14400, 7359}, // subsetneqq
    {14411, 7363}, // subsim
    {14418, 7367}, // subsub
    {14425, 7371}, // subsup
    {14432, 7375}, // succ
    {14437, 7379}, // succapprox
    {14448, 7383}, // succcurlyeq
    {14460, 7387}, // succeq
    {14467, 7391}, // succnapprox
    {14479, 7395}, // succneqq
    {14488, 7399}, // succnsim
    {14497, 7403}, // succsim
    {14505, 7407}, // sum
    {14509, 7411}, // sung
    {14514, 7415}, // sup
    {14518, 7419}, // sup1
    {14523, 7422}, // sup2
    {14528, 7425}, // sup3
    {14533, 7428}, // supE
    {14538, 7432}, // supdot
    {14545, 7436}, // supdsub
    {14553, 7440}, // supe
    {14558, 7444}, // supedot
    {14566, 7448}, // suphsol
    {14574, 7452}, // suphsub
    {14582, 7456}, // suplarr
    {14590, 7460}, // supmult
    {14598, 7464}, // supnE
    {14604, 7468}, // supne
    {14610, 7472}, // supplus
    {14618, 7476}, // supset
    {14625, 7480}, // supseteq
    {14634, 7484}, // supseteqq
    {14644, 7488}, // supsetneq
    {14654, 7492}, // supsetneqq
    {14665, 7496}, // supsim
    {14672, 7500}, // supsub
    {14679, 7504}, // supsup
    {14686, 7508}, // swArr
    {14692, 7512}, // swarhk
    {14699, 7516}, // swarr
    {14705, 7520}, // swarrow
    {14713, 7524}, // swnwar
    {14720, 7528}, // szlig
    {14726, 7531}, // target
    {14733, 7535}, // tau
    {14737, 7538}, // tbrk
    {14742, 7542}, // tcaron
    {14749, 7545}, // tcedil
    {14756, 7548}, // tcy
    {14760, 7551}, // tdot
    {14765, 7555}, // telrec
    {14772, 7559}, // tfr
    {14776, 7564}, // there4
    {14783, 7568}, // therefore
    {14793, 7572}, // theta
    {14799, 7575}, // thetasym
    {14808, 7578}, // thetav
    {14815, 7581}, // thickapprox
    {14827, 7585}, // thicksim
    {14836, 7589}, // thinsp
    {14843, 7593}, // thkap
    {14849, 7597}, // thksim
    {14856, 7601}, // thorn
    {14862, 7604}, // tilde
    {14868, 7607}, // times
    {14874, 7610}, // timesb
    {14881, 7614}, // timesbar
    {14890, 7618}, // timesd
    {14897, 7622}, // tint
    {14902, 7626}, // toea
    {14907, 7630}, // top
    {14911, 7634}, // topbot
    {14918, 7638}, // topcir
    {14925, 7642}, // topf
    {14930, 7647}, // topfork
    {14938, 7651}, // tosa
    {14943, 7655}, // tprime
    {14950, 7659}, // trade
    {14956, 7663}, // triangle
    {14965, 7667}, // triangledown
    {14978, 7671}, // triangleleft
    {14991, 7675}, // trianglelefteq
    {15006, 7679}, // triangleq
    {15016, 7683}, // triangleright
    {15030, 7687}, // trianglerighteq
    {15046, 7691}, // tridot
    {15053, 7695}, // trie
    {15058, 7699}, // triminus
    {15067, 7703}, // triplus
    {15075, 7707}, // trisb
    {15081, 7711}, // tritime
    {15089, 7715}, // trpezium
    {15098, 7719}, // tscr
    {15103, 7724}, // tscy
    {15108, 7727}, // tshcy
    {15114, 7730}, // tstrok
    {15121, 7733}, // twixt
    {15127, 7737}, // twoheadleftarrow
    {15144, 7741}, // twoheadrightarrow
    {15162, 7745}, // uArr
    {15167, 7749}, // uHar
    {15172, 7753}, // uacute
    {15179, 7756}, // uarr
    {15184, 7760}, // ubrcy
    {15190, 7763}, // ubreve
    {15197, 7766}, // ucirc
    {15203, 7769}, // ucy
    {15207, 7772}, // udarr
    {15213, 7776}, // udblac
    {15220, 7779}, // udhar
    {15226, 7783}, // ufisht
    {15233, 7787}, // ufr
    {15237, 7792}, // ugrave
    {15244, 7795}, // uharl
    {15250, 7799}, // uharr
    {15256, 7803}, // uhblk
    {15262, 7807}, // ulcorn
    {15269, 7811}, // ulcorner
    {15278, 7815}, // ulcrop
    {15285, 7819}, // ultri
    {15291, 7823}, // umacr
    {15297, 7826}, // uml
    {15301, 7829}, // uogon
    {15307, 7832}, // uopf
    {15312, 7837}, // uparrow
    {15320, 7841}, // updownarrow
    {15332, 7845}, // upharpoonleft
    {15346, 7849}, // upharpoonright
    {15361, 7853}, // uplus
    {15367, 7857}, // upsi
    {15372, 7860}, // upsih
    {15378, 7863}, // upsilon
    {15386, 7866}, // upuparrows
    {15397, 7870}, // urcorn
    {15404, 7874}, // urcorner
    {15413, 7878}, // urcrop
    {15420, 7882}, // uring
    {15426, 7885}, // urtri
    {15432, 7889}, // uscr
    {15437, 7894}, // utdot
    {15443, 7898}, // utilde
    {15450, 7901}, // utri
    {15455, 7905}, // utrif
    {15461, 7909}, // uuarr
    {15467, 7913}, // uuml
    {15472, 7916}, // uwangle
    {15480, 7920}, // vArr
    {15485, 7924}, // vBar
    {15490, 7928}, // vBarv
    {15496, 7932}, // vDash
    {15502, 7936}, // vangrt
    {15509, 7940}, // varepsilon
    {15520, 7943}, // varkappa
    {15529, 7946}, // varnothing
    {15540, 7950}, // varphi
    {15547, 7953}, // varpi
    {15553, 7956}, // varpropto
    {15563, 7960}, // varr
    {15568, 7964}, // varrho
    {15575, 7967}, // varsigma
    {15584, 7970}, // varsubsetneq
    {15597, 7977}, // varsubsetneqq
    {15611, 7984}, // varsupsetneq
    {15624, 7991}, // varsupsetneqq
    {15638, 7998}, // vartheta
    {15647, 8001}, // vartriangleleft
    {15663, 8005}, // vartriangleright
    {15680, 8009}, // vcy
    {15684, 8012}, // vdash
    {15690, 8016}, // vee
    {15694, 8020}, // veebar
    {15701, 8024}, // veeeq
    {15707, 8028}, // vellip
    {15714, 8032}, // verbar
    {15721, 8034}, // vert
    {15726, 8036}, // vfr
    {15730, 8041}, // vltri
    {15736, 8045}, // vnsub
    {15742, 8052}, // vnsup
    {15748, 8059}, // vopf
    {15753, 8064}, // vprop
    {15759, 8068}, // vrtri
    {15765, 8072}, // vscr
    {15770, 8077}, // vsubnE
    {15777, 8084}, // vsubne
    {15784, 8091}, // vsupnE
    {15791, 8098}, // vsupne
    {15798, 8105}, // vzigzag
    {15806, 8109}, // wcirc
    {15812, 8112}, // wedbar
    {15819, 8116}, // wedge
    {15825, 8120}, // wedgeq
    {15832, 8124}, // weierp
    {15839, 8128}, // wfr
    {15843, 8133}, // wopf
    {15848, 8138}, // wp
    {15851, 8142}, // wr
    {15854, 8146}, // wreath
    {15861, 8150}, // wscr
    {15866, 8155}, // xcap
    {15871, 8159}, // xcirc
    {15877, 8163}, // xcup
    {15882, 8167}, // xdtri
    {15888, 8171}, // xfr
    {15892, 8176}, // xhArr
    {15898, 8180}, // xharr
    {15904, 8184}, // xi
    {15907, 8187}, // xlArr
    {15913, 8191}, // xlarr
    {15919, 8195}, // xmap
    {15924, 8199}, // xnis
    {15929, 8203}, // xodot
    {15935, 8207}, // xopf
    {15940, 8212}, // xoplus
    {15947, 8216}, // xotime
    {15954, 8220}, // xrArr
    {15960, 8224}, // xrarr
    {15966, 8228}, // xscr
    {15971, 8233}, // xsqcup
    {15978, 8237}, // xuplus
    {15985, 8241}, // xutri
    {15991, 8245}, // xvee
    {15996, 8249}, // xwedge
    {16003, 8253}, // yacute
    {16010, 8256}, // yacy
    {16015, 8259}, // ycirc
    {16021, 8262}, // ycy
    {16025, 8265}, // yen
    {16029, 8268}, // yfr
    {16033, 8273}, // yicy
    {16038, 8276}, // yopf
    {16043, 8281}, // yscr
    {16048, 8286}, // yucy
    {16053, 8289}, // yuml
    {16058, 8292}, // zacute
    {16065, 8295}, // zcaron
    {16072, 8298}, // zcy
    {16076, 8301}, // zdot
    {16081, 8304}, // zeetrf
    {16088, 8308}, // zeta
    {16093, 8311}, // zfr
    {16097, 8316}, // zhcy
    {16102, 8319}, // zigrarr
    {16110, 8323}, // zopf
    {16115, 8328}, // zscr
    {16120, 8333}, // zwj
    {16124, 8337}, // zwnj
};

} // namespace markdown
