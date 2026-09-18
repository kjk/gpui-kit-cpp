/* Ports of the `autocorrect` crate's own tests (2.14.2): src/format.rs,
   src/rule/fullwidth.rs, src/rule/halfwidth.rs, src/rule/word.rs,
   src/config/toggle.rs, src/code/types.rs, src/code/code.rs,
   src/code/markdown.rs and src/ignorer.rs.

   The crate's test config (tests/.autocorrectrc.test) turns spellcheck on;
   the port runs the crate's *default* config, where spellcheck is off, so
   the two cases that would show `ios` → `iOS` assert the spacing-only
   result — src/autocorrect/readme.md records the difference. */

// The pair's header carries the port's internal.h behind the private-API
// gate; the toggle tests below reach into it.
#define GPUI_INCLUDE_PRIVATE_API 1
#include "Test.h"

// Not part of the gpui amalgam: the pair is compiled and linked only into
// the editor example and this test binary.
#include "autocorrect/autocorrect.h"

#include <stdio.h>
#include <string.h>

#if !GPUI_OS_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#endif

using base::Arena;
using base::ArenaDelete;
using base::ArenaNew;
using base::StrEq;

static Arena* gAcArena = nullptr;

static bool AcFormatEq(const char* input, const char* expected) {
    Str out = autocorrect::Format(gAcArena, Str(input));
    bool ok = StrEq(out, Str(expected));
    if (!ok) {
        printf("  format(\"%s\")\n    got      \"%.*s\"\n    expected \"%s\"\n",
               input, len(out), out.s, expected);
    }
    return ok;
}

// format.rs it_format + friends: the plain-text rule chain.
static void TestAutocorrectFormat() {
    TestSuite("autocorrect format");
    utassert(AcFormatEq("Hello世界.", "Hello 世界。"));
    utassert(AcFormatEq("!sm", "!sm"));
    utassert(AcFormatEq("Hello world!", "Hello world!"));
    utassert(AcFormatEq("部署到heroku有问题网页不能显示",
                        "部署到 heroku 有问题网页不能显示"));
    utassert(AcFormatEq("[北京]美企聘web大型应用开发高手-Ruby",
                        "[北京] 美企聘 web 大型应用开发高手-Ruby"));
    utassert(AcFormatEq("[成都](团800)招聘Rails工程师",
                        "[成都](团 800) 招聘 Rails 工程师"));
    utassert(AcFormatEq("Teahour.fm第18期发布", "Teahour.fm 第 18 期发布"));
    utassert(AcFormatEq("Yes!升级到了Rails 4", "Yes! 升级到了 Rails 4"));
    utassert(AcFormatEq("WWDC上讲到的Objective C/LLVM 改进",
                        "WWDC 上讲到的 Objective C/LLVM 改进"));
    utassert(AcFormatEq("在Ubuntu11.10 64位系统安装newrelic出错",
                        "在 Ubuntu11.10 64 位系统安装 newrelic 出错"));
    utassert(AcFormatEq("升级了macOS 10.9 附遇到的Bug概率有0.1%或更少",
                        "升级了 macOS 10.9 附遇到的 Bug 概率有 0.1% 或更少"));
    utassert(AcFormatEq(
        "在做Rails 3.2 Tutorial第Chapter 9.4.2遇到一个问题求助！",
        "在做 Rails 3.2 Tutorial 第 Chapter 9.4.2 遇到一个问题求助！"));
    utassert(AcFormatEq("发现macOS安装软件新方法：Homebrew",
                        "发现 macOS 安装软件新方法：Homebrew"));
    utassert(
        AcFormatEq("Without looking like it’s ‘been’ marked up with tags or "
                   "“formatting instructions”.",
                   "Without looking like it’s ‘been’ marked up with tags or "
                   "“formatting instructions”."));
    utassert(AcFormatEq(
        "隔夜SHIBOR报1.5530%，上涨33.80%个基点。7天SHIBOR报2.3200%，上涨6."
        "10个基点。3个月SHIBOR报2.8810%，下降1.80个",
        "隔夜 SHIBOR 报 1.5530%，上涨 33.80% 个基点。7 天 SHIBOR 报 "
        "2.3200%，上涨 6.10 个基点。3 个月 SHIBOR 报 2.8810%，下降 1.80 个"));
    utassert(
        AcFormatEq("适用于“无声音”问题的iPhone 12和iPhone 12 Pro服务计划",
                   "适用于“无声音”问题的 iPhone 12 和 iPhone 12 Pro 服务计划"));
    utassert(AcFormatEq(
        "野村：重申吉利汽车(00175)“买入”评级 上调目标价至17.9港元",
        "野村：重申吉利汽车 (00175)“买入”评级 上调目标价至 17.9 港元"));
    utassert(AcFormatEq("小米集团-W调整目标价为13.5港币",
                        "小米集团-W 调整目标价为 13.5 港币"));
    utassert(
        AcFormatEq("（路透社）-预计全年净亏损约1.3亿港元*预期因出售汽车",
                   "（路透社）- 预计全年净亏损约 1.3 亿港元*预期因出售汽车"));
    utassert(AcFormatEq(
        "（路透社）-预计全年净亏损约1.3亿\n\n港元*预期因出售汽车",
        "（路透社）- 预计全年净亏损约 1.3 亿\n\n港元*预期因出售汽车"));
    utassert(AcFormatEq("Cell或RefCell类型使用某种形式的*内部",
                        "Cell 或 RefCell 类型使用某种形式的*内部"));
    utassert(AcFormatEq("Cell或RefCell类型使用某种形式的*内部可变性*",
                        "Cell 或 RefCell 类型使用某种形式的*内部可变性*"));

    // it_format_for_specials
    utassert(AcFormatEq("记事本,记事本显示阅读次数#149",
                        "记事本，记事本显示阅读次数#149"));
    utassert(AcFormatEq("HashTag的演示#标签", "HashTag 的演示#标签"));
    utassert(
        AcFormatEq("HashTag 的演示#标签#演示", "HashTag 的演示#标签#演示"));
    utassert(AcFormatEq("Mention里面有关于中文的@某某人",
                        "Mention 里面有关于中文的@某某人"));
    utassert(AcFormatEq("Mention里面有关于中文的 @huacnlee 测试",
                        "Mention 里面有关于中文的 @huacnlee 测试"));
    utassert(AcFormatEq("Dollar的演示$阿里巴巴.US$股票标签",
                        "Dollar 的演示$阿里巴巴.US$股票标签"));
    utassert(
        AcFormatEq("测试英文,逗号Comma转换.", "测试英文，逗号 Comma 转换。"));
    utassert(
        AcFormatEq("测试英文,Comma逗号转换.", "测试英文，Comma 逗号转换。"));
    utassert(AcFormatEq("英文,逗号后面.阿里巴巴.US有空格?的情况!测试",
                        "英文，逗号后面。阿里巴巴.US 有空格？的情况！测试"));
    utassert(AcFormatEq("你好hello?world!", "你好 hello?world!"));
    utassert(AcFormatEq("search by%关键词%", "search by%关键词%"));

    // it_format_for_programming
    utassert(AcFormatEq("A开头的case测试", "A 开头的 case 测试"));
    utassert(AcFormatEq("内容带有\n不会处理", "内容带有\n不会处理"));
    utassert(AcFormatEq(
        "内容带有%s或%d或%v特殊字符，或者%S或%D或%V这些特殊format字符",
        "内容带有%s或%d或%v特殊字符，或者%S或%D或%V这些特殊 format 字符"));
    utassert(
        AcFormatEq("内容带有$1或$2或$3特殊字符", "内容带有$1或$2或$3特殊字符"));
    utassert(AcFormatEq("来自Yahoo!的文档", "来自 Yahoo! 的文档"));
    utassert(AcFormatEq("规则后面是否跟随者!import以及规则的来源",
                        "规则后面是否跟随者!import 以及规则的来源"));
    utassert(AcFormatEq("对比C++的差别，结果有2+差别",
                        "对比 C++ 的差别，结果有 2+ 差别"));
    utassert(AcFormatEq("交叉编译的 C/C++编译", "交叉编译的 C/C++ 编译"));

    // it_format_for_date
    utassert(AcFormatEq("于3月10日开始", "于 3 月 10 日开始"));
    utassert(AcFormatEq("于3月开始", "于 3 月开始"));
    utassert(AcFormatEq("于2009年开始", "于 2009 年开始"));
    utassert(AcFormatEq("正式发布2013年3月10日-Ruby Saturday活动召集",
                        "正式发布 2013 年 3 月 10 日-Ruby Saturday 活动召集"));
    utassert(AcFormatEq("正式发布2013年3月10号发布",
                        "正式发布 2013 年 3 月 10 号发布"));
    utassert(
        AcFormatEq("2013年12月22号开始出发", "2013 年 12 月 22 号开始出发"));
    utassert(AcFormatEq("12月22号开始出发", "12 月 22 号开始出发"));
    utassert(AcFormatEq("22号开始出发", "22 号开始出发"));

    // it_format_with_markdown_td
    utassert(AcFormatEq("| 8 位有符号整数（补码）   |",
                        "| 8 位有符号整数（补码）   |"));
    utassert(
        AcFormatEq("| 8 位有符号整数（补码） |", "| 8 位有符号整数（补码） |"));
    utassert(AcFormatEq("| 8 位有符号整数   |", "| 8 位有符号整数   |"));
    utassert(AcFormatEq("| 8 位有符号整数。   |", "| 8 位有符号整数。   |"));
    utassert(
        AcFormatEq("| 包括 8 位有符号整数。 |", "| 包括 8 位有符号整数。 |"));
    utassert(AcFormatEq("|   包括 8 位有符号整数！   |",
                        "|   包括 8 位有符号整数！   |"));
    utassert(AcFormatEq("| 64 位浮点数（例如：10.90）| `double` |",
                        "| 64 位浮点数（例如：10.90）| `double` |"));

    // it_format_for_remove_spaces_with_punctuation
    utassert(
        AcFormatEq("以达到快速、 跨平台 、 低资源占用的目的 。 "
                   "很多著名且受欢迎的软件，例如 Firefox 、 Dropbox 和 "
                   "Cloudflare 都在使用",
                   "以达到快速、跨平台、低资源占用的目的。很多著名且受欢迎的软"
                   "件，例如 Firefox、Dropbox 和 Cloudflare 都在使用"));
    utassert(
        AcFormatEq("注意： 引进给变量， 转换为机器代码。 这意味着任何变量、 "
                   "常量； 命名的概念都会被删除",
                   "注意：引进给变量，转换为机器代码。这意味着任何变量、常量；"
                   "命名的概念都会被删除"));
    utassert(
        AcFormatEq("注意 ： 引进给变量 ， 转换为机器代码 。 这意味着任何变量 "
                   "、 常量 ； 命名的概念都会被删除",
                   "注意：引进给变量，转换为机器代码。这意味着任何变量、常量；"
                   "命名的概念都会被删除"));
    utassert(
        AcFormatEq("测试测试， `code` 测试测试", "测试测试，`code` 测试测试"));

    // it_format_for_number
    utassert(AcFormatEq("在Ubuntu 11.10 64位系统安装Go出错",
                        "在 Ubuntu 11.10 64 位系统安装 Go 出错"));
    utassert(AcFormatEq("喜欢暗黑2却对 D3不满意的可以看看这个。",
                        "喜欢暗黑 2 却对 D3 不满意的可以看看这个。"));
    utassert(AcFormatEq("Ruby 2.7版本第3次发布", "Ruby 2.7 版本第 3 次发布"));
    utassert(AcFormatEq("值范围-255或+255之间", "值范围 -255 或 +255 之间"));

    // it_format_for_special_symbols
    utassert(
        AcFormatEq("公告:(美股)阿里巴巴[BABA.US]发布2019下半年财报!",
                   "公告:(美股) 阿里巴巴 [BABA.US] 发布 2019 下半年财报！"));
    utassert(AcFormatEq("消息github.com解禁了", "消息 github.com 解禁了"));
    utassert(AcFormatEq(
        "美股异动|阿帕奇石油(APA.US)盘前涨超15% 在苏里南近海发现大量石油",
        "美股异动 | 阿帕奇石油 (APA.US) 盘前涨超 15% "
        "在苏里南近海发现大量石油"));
    utassert(AcFormatEq(
        "美国统计局：美国11月原油出口下降至302.3万桶/日，10月为338.3万桶/日。",
        "美国统计局：美国 11 月原油出口下降至 302.3 万桶/日，10 月为 338.3 "
        "万桶/日。"));
    utassert(AcFormatEq("[b]Foo bar dar[/b]", "[b]Foo bar dar[/b]"));

    // it_format_for_fullwidth_symbols
    utassert(AcFormatEq(
        "（美股）市场：发布「最新」100消息【BABA.US】“大涨”50%；同比上涨20%！",
        "（美股）市场：发布「最新」100 消息【BABA.US】“大涨”50%；同比上涨 "
        "20%！"));
    utassert(AcFormatEq("第3季度财报发布看涨看跌？敬请期待。",
                        "第 3 季度财报发布看涨看跌？敬请期待。"));

    // it_format_for_space_dash_with_hans
    utassert(AcFormatEq("范围包含A-Z等字符", "范围包含 A-Z 等字符"));
    utassert(AcFormatEq("第3季度-财报发布看涨看跌？敬请期待。",
                        "第 3 季度 - 财报发布看涨看跌？敬请期待。"));
    utassert(AcFormatEq("腾讯-ADR-已发行", "腾讯-ADR-已发行"));
    utassert(AcFormatEq("（腾讯）-发布-（新版）本微信",
                        "（腾讯）- 发布 -（新版）本微信"));
    utassert(AcFormatEq("【腾讯】-发布-【新版】本微信",
                        "【腾讯】- 发布 -【新版】本微信"));
    utassert(AcFormatEq("「腾讯」-发布-「新版」本微信",
                        "「腾讯」- 发布 -「新版」本微信"));
    utassert(AcFormatEq("《腾讯》-发布-《新版》本微信",
                        "《腾讯》- 发布 -《新版》本微信"));
    utassert(
        AcFormatEq("“腾讯”-发布-“新版”本微信", "“腾讯” - 发布 - “新版”本微信"));
    utassert(
        AcFormatEq("‘腾讯’-发布-‘新版’本微信", "‘腾讯’ - 发布 - ‘新版’本微信"));
    utassert(AcFormatEq("行内`code`代码", "行内 `code` 代码"));

    // it_format_for_url
    utassert(AcFormatEq("wiki/网页浏览器列表#基於WebKit排版引擎",
                        "wiki/网页浏览器列表#基於WebKit排版引擎"));
    utassert(AcFormatEq("wiki-/_网页浏基於WebKit排版",
                        "wiki-/_网页浏基於WebKit排版"));
    utassert(AcFormatEq("Web/CSS/网格-模板-列", "Web/CSS/网格-模板-列"));
    utassert(AcFormatEq("wiki/hello网页浏览器列表#基於WebKit排版引擎",
                        "wiki/hello网页浏览器列表#基於WebKit排版引擎"));
    utassert(AcFormatEq("URL地址 /wiki/hello网页浏览器 访问",
                        "URL 地址 /wiki/hello网页浏览器 访问"));
    utassert(AcFormatEq(
        "请打开URL地址 https://google.com/这是URL文件名.html 访问",
        "请打开 URL 地址 https://google.com/这是URL文件名.html 访问"));
    utassert(AcFormatEq("https://google.com/这是URL文件名.html",
                        "https://google.com/这是URL文件名.html"));
    utassert(AcFormatEq(
        "https://zh.wikipedia.org/wiki/网页浏览器列表#基於WebKit排版引擎",
        "https://zh.wikipedia.org/wiki/网页浏览器列表#基於WebKit排版引擎"));
    utassert(AcFormatEq("foo-bar_01.htm#测试copy", "foo-bar_01.htm#测试copy"));
    utassert(AcFormatEq("foo-bar_01#copy测试", "foo-bar_01#copy测试"));
    utassert(
        AcFormatEq("ch04-01-what-is-ownership.html#只在堆疊上的資料拷貝copy",
                   "ch04-01-what-is-ownership.html#只在堆疊上的資料拷貝copy"));
    utassert(AcFormatEq("中文A/B中文", "中文 A/B 中文"));
    utassert(
        AcFormatEq("在HTTP/2与HTTP/1.1里面", "在 HTTP/2 与 HTTP/1.1 里面"));
    utassert(AcFormatEq("//this is注释", "//this is 注释"));
    utassert(AcFormatEq("记事本,记事本1显示阅读次数#149号",
                        "记事本，记事本 1 显示阅读次数#149 号"));
    utassert(AcFormatEq("HashTag的演示#标签1", "HashTag 的演示#标签 1"));

    // it_format_for_cjk (one per script family)
    utassert(AcFormatEq(
        "全世界已有数百家公司在生产环境中使用Rust，以达到快速、跨平台、低资源占"
        "用的目的。很多著名且受欢迎的软件，例如Firefox、 "
        "Dropbox和Cloudflare都在使用Rust。",
        "全世界已有数百家公司在生产环境中使用 "
        "Rust，以达到快速、跨平台、低资源占用的目的。很多著名且受欢迎的软件，例"
        "如 Firefox、Dropbox 和 Cloudflare 都在使用 Rust。"));
    utassert(AcFormatEq(
        "既に、世界中の数百という企業がRustを採用し、高速で低リソースのクロスプ"
        "ラットフォームソリューションを実現しています。皆さんがご存じで愛用して"
        "いるソフトウェア、例えばFirefox、DropboxやCloudflareも、Rustを採用して"
        "います。",
        "既に、世界中の数百という企業が Rust "
        "を採用し、高速で低リソースのクロスプラットフォームソリューションを実現"
        "しています。皆さんがご存じで愛用しているソフトウェア、例えば "
        "Firefox、Dropbox や Cloudflare も、Rust を採用しています。"));
    utassert(AcFormatEq(
        "전 세계 수백 개의 회사가 프로덕션 환경에서 Rust를 사용하여 빠르고, "
        "크로스 플랫폼 및 낮은 리소스 사용량을 달성했습니다. Firefox, Dropbox "
        "및 Cloudflare와 같이 잘 알려져 있고 널리 사용되는 많은 소프트웨어가 "
        "Rust를 사용하고 있습니다.",
        "전 세계 수백 개의 회사가 프로덕션 환경에서 Rust 를 사용하여 빠르고, "
        "크로스 플랫폼 및 낮은 리소스 사용량을 달성했습니다. Firefox, Dropbox "
        "및 Cloudflare 와 같이 잘 알려져 있고 널리 사용되는 많은 소프트웨어가 "
        "Rust 를 사용하고 있습니다."));

    // test_format_halfwidth
    utassert(AcFormatEq("你好\nWelcome all say hello，world。\n你好world",
                        "你好\nWelcome all say hello, world.\n你好 world"));
    utassert(
        AcFormatEq("Jetbrains请访问：https://www.jetbrains.com/help/idea/"
                   "using-git-integration.html。",
                   "Jetbrains "
                   "请访问：https://www.jetbrains.com/help/idea/"
                   "using-git-integration.html。"));
}

// rule/fullwidth.rs + rule/halfwidth.rs module tests, through the same
// public Format (these rules are in the default chain).
static void TestAutocorrectWidthRules() {
    TestSuite("autocorrect width rules");
    // fullwidth
    utassert(AcFormatEq("你好,这是一个句子.", "你好，这是一个句子。"));
    utassert(AcFormatEq("\"请求参数错误.\"", "\"请求参数错误。\""));
    utassert(AcFormatEq("'请求参数错误.'", "'请求参数错误。'"));
    // The crate's fullwidth module test runs the one rule and keeps "!开"
    // as-is; the whole chain's space-punctuation rule also spaces the "!".
    utassert(AcFormatEq("!开头不处理.", "! 开头不处理。"));
    utassert(AcFormatEq("刚刚买了一部 iPhone,好开心!",
                        "刚刚买了一部 iPhone，好开心！"));
    utassert(AcFormatEq("蚂蚁集团上市后有多大的上涨空间?",
                        "蚂蚁集团上市后有多大的上涨空间？"));
    utassert(AcFormatEq("蚂蚁疾奔:蚂蚁集团两地上市~全速推进!",
                        "蚂蚁疾奔：蚂蚁集团两地上市~全速推进！"));
    utassert(AcFormatEq("蚂蚁集团是阿里巴巴 (BABA.N) 旗下金融科技子公司",
                        "蚂蚁集团是阿里巴巴 (BABA.N) 旗下金融科技子公司"));
    utassert(
        AcFormatEq("确保&quot;&gt;HTML "
                   "Entity&lt;&quot;的字符&#34;不会被处理&#34; Ruby&amp;Go",
                   "确保&quot;&gt;HTML "
                   "Entity&lt;&quot;的字符&#34;不会被处理&#34; Ruby&amp;Go"));
    utassert(AcFormatEq("中文;中文", "中文;中文"));
    utassert(AcFormatEq("你好,這是一個句子.", "你好，這是一個句子。"));
    utassert(
        AcFormatEq("でもっと多くのことができるようになります."
                   "そんな新機能の数々をさっそく体験してみましょう.",
                   "でもっと多くのことができるようになります。そんな新機能の数"
                   "々をさっそく体験してみましょう。"));
    utassert(AcFormatEq("근면, 검소, 협동은 우리 겨레의 미덕이다.",
                        "근면, 검소, 협동은 우리 겨레의 미덕이다."));

    // halfwidth-word
    utassert(AcFormatEq(
        "测试:"
        "ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚＡＢＣＤＥＦＧＨＩ"
        "ＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺ１２３４５６７８９０",
        "测试:abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"));
    utassert(AcFormatEq("他说：我们将在１６：３２分出发去ＣＢＤ中心。",
                        "他说：我们将在16:32分出发去CBD中心。"));
    utassert(AcFormatEq(
        "ジョイフル－後場売り気配　200 店舗を閉鎖へ　7 月以降、不採算店中心に",
        "ジョイフル－後場売り気配 200 店舗を閉鎖へ 7 月以降、不採算店中心に"));

    // halfwidth-punctuation ignores
    utassert(AcFormatEq("。", "。"));
    utassert(AcFormatEq("，", "，"));
    utassert(AcFormatEq("SHA1。", "SHA1。"));
    utassert(AcFormatEq("a。", "a。"));
    utassert(AcFormatEq("foo-bar-dar。", "foo-bar-dar。"));
    utassert(AcFormatEq("hello)。", "hello)。"));
    utassert(AcFormatEq("说：你好 english。", "说：你好 english。"));
    utassert(AcFormatEq("${item.name}（ID ${item.id}）",
                        "${item.name}（ID ${item.id}）"));
    utassert(AcFormatEq("{{ t('name') }}：{{ item.extraKeys.join(' | ') }}",
                        "{{ t('name') }}：{{ item.extraKeys.join(' | ') }}"));
    utassert(AcFormatEq("The Exchange’s", "The Exchange’s"));
    utassert(AcFormatEq(
        "It's revenue \"conditions\" among the suppliers’ “customers”",
        "It's revenue \"conditions\" among the suppliers’ “customers”"));

    // halfwidth-punctuation conversions
    utassert(AcFormatEq("hello。", "hello。"));
    utassert(AcFormatEq("hello 你好。", "hello 你好。"));
    // Same: the module test shows the punctuation half only; the chain also
    // spaces 文1 / 文2.
    utassert(AcFormatEq("中文1\nhello world。\n中文2",
                        "中文 1\nhello world.\n中文 2"));
    utassert(AcFormatEq("  \n  Said：Come and，Join us！  \n  ",
                        "  \n  Said: Come and, Join us!  \n  "));
    utassert(
        AcFormatEq("Said：Come and，Join us！", "Said: Come and, Join us!"));
    utassert(AcFormatEq("_（HTML5 Rocks）_", "_(HTML5 Rocks)_"));
    utassert(AcFormatEq("  Start with space next word？Join us?",
                        "  Start with space next word? Join us?"));
    utassert(AcFormatEq(", Not start with word will not change。",
                        ", Not start with word will not change。"));
    utassert(AcFormatEq("：“Not start with word will not change”",
                        "：“Not start with word will not change”"));
    utassert(AcFormatEq("Come and， Join us！", "Come and, Join us!"));
    utassert(
        AcFormatEq("The microphone or camera is occupied，Please check and "
                   "re-record the video。",
                   "The microphone or camera is occupied, Please check and "
                   "re-record the video."));
    utassert(AcFormatEq("The “Convertible Amount” case。",
                        "The “Convertible Amount” case."));
    utassert(AcFormatEq("The“Convertible Amount”case。",
                        "The“Convertible Amount”case."));
    utassert(AcFormatEq("The（Convertible Amount）case！",
                        "The (Convertible Amount) case!"));
    utassert(AcFormatEq("The【Convertible Amount】case？",
                        "The [Convertible Amount] case?"));
    utassert(AcFormatEq("The「Convertible Amount」case：",
                        "The [Convertible Amount] case:"));
    utassert(AcFormatEq("The《Convertible Amount》case，",
                        "The “Convertible Amount” case,"));
    utassert(AcFormatEq("Reason: CORS header ‘Origin’ cannot be added",
                        "Reason: CORS header ‘Origin’ cannot be added"));

    // test_ignore_left_quote_in_last
    utassert(AcFormatEq("Escher puzzle (", "Escher puzzle ("));
    utassert(AcFormatEq("Escher puzzle【", "Escher puzzle【"));
    utassert(AcFormatEq("Escher puzzle《", "Escher puzzle《"));
    utassert(AcFormatEq("Escher puzzle“", "Escher puzzle“"));
    utassert(AcFormatEq("Escher puzzle‘", "Escher puzzle‘"));
    utassert(AcFormatEq("Escher puzzle「", "Escher puzzle「"));

    // test_halfwidth_punctuation_with_in_quote
    utassert(AcFormatEq("\"，\"", "\"，\""));
    utassert(AcFormatEq("\"。\"", "\"。\""));
    utassert(AcFormatEq("\"a。\"", "\"a。\""));
    utassert(AcFormatEq("\"Hi！\"", "\"Hi!\""));
    utassert(AcFormatEq("\"hello-world。\"", "\"hello-world.\""));
    utassert(AcFormatEq("'hello “world”。'", "'hello “world”.'"));
    utassert(AcFormatEq("\"hello “world”。\"", "\"hello “world”.\""));
    utassert(AcFormatEq("\"hello ‘world’。\"", "\"hello ‘world’.\""));
    utassert(AcFormatEq("'hello ‘world’。'", "'hello ‘world’.'"));
    utassert(AcFormatEq("\"Only the first time break。\"",
                        "\"Only the first time break.\""));
    utassert(AcFormatEq("'Only the first time break？'",
                        "'Only the first time break?'"));
    utassert(AcFormatEq("`Only the first time break！`",
                        "`Only the first time break!`"));
    utassert(AcFormatEq("`${this.$t('hello')}：${items.join('，')}`",
                        "`${this.$t('hello')}：${items.join('，')}`"));
    utassert(AcFormatEq("`${t('hello')}：${user.name}`",
                        "`${t('hello')}：${user.name}`"));
    utassert(
        AcFormatEq("\"#{vars.join(\"，\")}\"", "\"#{vars.join(\"，\")}\""));
}

// config/toggle.rs.
static void TestAutocorrectToggle() {
    TestSuite("autocorrect toggle");
    using autocorrect::Toggle;
    using autocorrect::ToggleKind;
    using autocorrect::ToggleParse;

    Toggle t = ToggleParse(StrL("autocorrect-enable"));
    utassert(t.kind == ToggleKind::Enable && t.RulesEmpty());
    t = ToggleParse(StrL("// autocorrect-enable"));
    utassert(t.kind == ToggleKind::Enable && t.RulesEmpty());
    t = ToggleParse(StrL("# autocorrect: true"));
    utassert(t.kind == ToggleKind::Enable && t.RulesEmpty());
    t = ToggleParse(StrL("# autocorrect:true"));
    utassert(t.kind == ToggleKind::Enable && t.RulesEmpty());
    t = ToggleParse(StrL("# autocorrect: false"));
    utassert(t.kind == ToggleKind::Disable && t.RulesEmpty());
    t = ToggleParse(StrL("# autocorrect-disable"));
    utassert(t.kind == ToggleKind::Disable && t.RulesEmpty());
    t = ToggleParse(StrL("// hello world"));
    utassert(t.kind == ToggleKind::None);

    t = ToggleParse(StrL("// autocorrect-disable space-word,fullwidth"));
    utassert(t.kind == ToggleKind::Disable);
    utassert(autocorrect::ToggleDisableRules(&t) ==
             ((1u << autocorrect::kRuleSpaceWord) |
              (1u << autocorrect::kRuleFullwidth)));
    utassert(autocorrect::ToggleIsEnabled(&t));

    // Unknown rule names still make the set non-empty.
    t = ToggleParse(StrL("// autocorrect-enable foo, bar"));
    utassert(t.kind == ToggleKind::Enable && !t.RulesEmpty());
    utassert(!autocorrect::ToggleIsEnabled(&t));
}

// code/types.rs.
static void TestAutocorrectTypes() {
    TestSuite("autocorrect types");
    using autocorrect::GetFileExtension;
    using autocorrect::IsSupportType;
    using autocorrect::MatchFilename;
    Arena* a = gAcArena;

    utassert(IsSupportType(StrL("html")));
    utassert(IsSupportType(StrL("htm")));
    utassert(IsSupportType(StrL("html.erb")));
    utassert(IsSupportType(StrL("rust")));
    utassert(IsSupportType(StrL("rs")));
    utassert(IsSupportType(StrL("jupyter")));
    utassert(IsSupportType(StrL("ipynb")));
    utassert(!IsSupportType(StrL("foo")));
    utassert(!IsSupportType(StrL("index.html")));
    utassert(!IsSupportType(StrL("gettext")));

    utassert(StrEq(GetFileExtension(a, StrL("text")), StrL("text")));
    utassert(StrEq(GetFileExtension(a, StrL("txt")), StrL("txt")));
    utassert(StrEq(GetFileExtension(a, StrL("/foo/bar/dar.rb")), StrL("rb")));
    utassert(
        StrEq(GetFileExtension(a, StrL("/foo/bar/aaa.dar.rb")), StrL("rb")));
    utassert(StrEq(GetFileExtension(a, StrL("/foo/bar/dar.html.erb")),
                   StrL("html.erb")));
    utassert(StrEq(GetFileExtension(a, StrL("html.erb")), StrL("html.erb")));
    utassert(StrEq(GetFileExtension(a, StrL("Gemfile")), StrL("Gemfile")));
    utassert(StrEq(GetFileExtension(a, StrL("/dar.js")), StrL("js")));
    utassert(StrEq(GetFileExtension(a, StrL("/foo/bar/dar")), StrL("dar")));

    utassert(StrEq(MatchFilename(a, StrL("app.md")), StrL("markdown")));
    utassert(StrEq(MatchFilename(a, StrL("app.mdx")), StrL("markdown")));
    utassert(StrEq(MatchFilename(a, StrL("app.htm")), StrL("html")));
    utassert(StrEq(MatchFilename(a, StrL("app.html")), StrL("html")));
    utassert(StrEq(MatchFilename(a, StrL("app.html.erb")), StrL("html")));
    utassert(StrEq(MatchFilename(a, StrL("app.js")), StrL("javascript")));
    utassert(StrEq(MatchFilename(a, StrL("app.ts")), StrL("javascript")));
    utassert(StrEq(MatchFilename(a, StrL("app.js.erb")), StrL("javascript")));
    utassert(StrEq(MatchFilename(a, StrL("app.properties")), StrL("conf")));
    utassert(StrEq(MatchFilename(a, StrL("app.ini")), StrL("conf")));
    utassert(StrEq(MatchFilename(a, StrL("app.toml")), StrL("conf")));
    utassert(StrEq(MatchFilename(a, StrL("app.strings")), StrL("strings")));
    utassert(StrEq(MatchFilename(a, StrL("app.py")), StrL("python")));
    utassert(StrEq(MatchFilename(a, StrL("main.proto")), StrL("java")));
    utassert(StrEq(MatchFilename(a, StrL("app.gradle")), StrL("kotlin")));
    utassert(StrEq(MatchFilename(a, StrL("app.kt")), StrL("kotlin")));
    utassert(StrEq(MatchFilename(a, StrL("zh-CN.xml")), StrL("xml")));
    utassert(StrEq(MatchFilename(a, StrL("bar.adoc")), StrL("asciidoc")));
    utassert(StrEq(MatchFilename(a, StrL("bar.tex")), StrL("latex")));
    utassert(StrEq(MatchFilename(a, StrL("bar.pot")), StrL("gettext")));
    utassert(StrEq(MatchFilename(a, StrL("Gemfile")), StrL("ruby")));
    utassert(StrEq(MatchFilename(a, StrL("Rakefile")), StrL("ruby")));
    utassert(StrEq(MatchFilename(a, StrL("foo.gemspec")), StrL("ruby")));
    utassert(
        StrEq(MatchFilename(a, StrL("./foo/bar.jupyter")), StrL("jupyter")));
    utassert(StrEq(MatchFilename(a, StrL("./foo/bar.ipynb")), StrL("jupyter")));
}

// code/code.rs test_format_for / test_lint_for, and lib.rs it_lint_for.
static void TestAutocorrectCode() {
    TestSuite("autocorrect code");
    Arena* a = gAcArena;
    using autocorrect::FormatFor;
    using autocorrect::LintFor;

    utassert(StrEq(FormatFor(a, StrL("// Hello你好"), StrL("rust")).out,
                   StrL("// Hello 你好")));
    utassert(StrEq(FormatFor(a, StrL("// Hello你好"), StrL("js")).out,
                   StrL("// Hello 你好")));
    utassert(StrEq(FormatFor(a, StrL("// Hello你好"), StrL("ruby")).out,
                   StrL("// Hello你好")));
    utassert(
        StrEq(FormatFor(a, StrL("// Hello你好"), StrL("not-exist-type")).out,
              StrL("// Hello你好")));

    utassert(LintFor(a, StrL("// Hello你好"), StrL("rust")).nLines == 1);
    utassert(LintFor(a, StrL("// Hello你好"), StrL("js")).nLines == 1);
    utassert(LintFor(a, StrL("// Hello你好"), StrL("ruby")).nLines == 0);
    utassert(LintFor(a, StrL("// Hello你好"), StrL("not-exist-type"))
                 .nLines == 0);

    // lib.rs it_lint_for. The crate's test config turns spellcheck on and
    // shows `iOS`; the default config this port runs has it off, so the
    // spacing-only result is asserted (see the readme).
    autocorrect::LintResult r =
        LintFor(a, StrL("<p>Hello你好ios版本</p>"), StrL("foo.bar.html"));
    utassert(!r.HasError());
    utassert(r.nLines == 1);
    if (r.nLines == 1) {
        utassert(r.lines[0].line == 1);
        utassert(r.lines[0].col == 4);
        utassert(StrEq(r.lines[0].old, StrL("Hello你好ios版本")));
        utassert(StrEq(r.lines[0].neu, StrL("Hello 你好 ios 版本")));
        utassert(r.lines[0].severity == autocorrect::Severity::Error);
    }
    utassert(StrEq(r.filepath, StrL("foo.bar.html")));

    r = LintFor(a, StrL("const a = 'hello世界'"), StrL("js"));
    utassert(!r.HasError());
    utassert(r.nLines == 1);
    if (r.nLines == 1) {
        utassert(r.lines[0].line == 1);
        utassert(r.lines[0].col == 11);
        utassert(StrEq(r.lines[0].old, StrL("'hello世界'")));
        utassert(StrEq(r.lines[0].neu, StrL("'hello 世界'")));
    }

    utassert(
        StrEq(FormatFor(a, StrL("<p>Hello你好</p>"), StrL("foo.bar.html")).out,
              StrL("<p>Hello 你好</p>")));
    utassert(StrEq(FormatFor(a, StrL("const a = 'hello世界'"), StrL("js")).out,
                   StrL("const a = 'hello 世界'")));

    // lib.rs test_format_for: "text" lints as markdown.
    utassert(StrEq(FormatFor(a, StrL("Hello世界."), StrL("text")).out,
                   StrL("Hello 世界。")));

    // code.rs test_disable_rules_all.
    const char* disableRaw = R"js(// autocorrect-disable
        // hello世界
        // autocorrect-enable
        // hello世界
        // autocorrect-disable space-word
        // hello世界.
        // autocorrect-disable fullwidth
        // hello世界.
        // autocorrect-disable space-word,fullwidth
        // hello世界.
        const a = "hello世界."
        “)js";
    const char* disableExpected = R"js(// autocorrect-disable
        // hello世界
        // autocorrect-enable
        // hello 世界
        // autocorrect-disable space-word
        // hello世界。
        // autocorrect-disable fullwidth
        // hello 世界.
        // autocorrect-disable space-word,fullwidth
        // hello世界.
        const a = "hello世界."
        “)js";
    utassert(StrEq(FormatFor(a, Str(disableRaw), StrL("js")).out,
                   Str(disableExpected)));
    r = LintFor(a, Str(disableRaw), StrL("js"));
    utassert(r.nLines == 3);
    if (r.nLines == 3) {
        utassert(StrEq(r.lines[0].neu, StrL("// hello 世界")));
        utassert(StrEq(r.lines[1].neu, StrL("// hello世界。")));
        utassert(StrEq(r.lines[2].neu, StrL("// hello 世界.")));
    }
}

// code.rs test_inline_script_line_number: codeblock lines land at the
// block's own position, in the block's own language.
static void TestAutocorrectMarkdownLint() {
    TestSuite("autocorrect markdown lint");
    Arena* a = gAcArena;
    const char* raw = R"md(Hello world

```ts
// hello世界
const a = "string字符串";
```

### 外部test

Second line

```rb
class User
    # 查找user
    def find
    end
end
```
)md";
    autocorrect::LintResult r = autocorrect::LintFor(a, Str(raw), StrL("md"));
    utassert(!r.HasError());
    utassert(r.nLines == 4);
    if (r.nLines == 4) {
        utassert(r.lines[0].line == 4 && r.lines[0].col == 1);
        utassert(StrEq(r.lines[0].neu, StrL("// hello 世界")));
        utassert(StrEq(r.lines[0].old, StrL("// hello世界")));
        utassert(r.lines[1].line == 5 && r.lines[1].col == 11);
        utassert(StrEq(r.lines[1].neu, StrL("\"string 字符串\"")));
        utassert(StrEq(r.lines[1].old, StrL("\"string字符串\"")));
        utassert(r.lines[2].line == 8 && r.lines[2].col == 5);
        utassert(StrEq(r.lines[2].neu, StrL("外部 test")));
        utassert(StrEq(r.lines[2].old, StrL("外部test")));
        utassert(r.lines[3].line == 14 && r.lines[3].col == 5);
        utassert(StrEq(r.lines[3].neu, StrL("# 查找 user")));
        utassert(StrEq(r.lines[3].old, StrL("# 查找user")));
        for (int i = 0; i < 4; i++) {
            utassert(r.lines[i].severity == autocorrect::Severity::Error);
        }
    }
}

// ignorer.rs, over a fixture in a directory of the test's own. It used to
// write .gitignore straight into the working directory on the assumption
// that cwd is always the out dir; running the binary directly from a
// checkout then overwrote and deleted the repository's own .gitignore. A
// test never writes a dotfile into a directory it does not own.
static const char* kIgnoreDir = "autocorrect_ignorer_test_root";

static bool WriteIgnoreFile(const char* name, const char* content) {
    TempStr path = fmt("%s/%s", Str(kIgnoreDir), Str(name));
    FILE* f = fopen(path.s, "wb");
    if (!f) {
        return false;
    }
    size_t len = strlen(content);
    bool ok = fwrite(content, 1, len, f) == len;
    fclose(f);
    return ok;
}

static void RemoveIgnoreFile(const char* name) {
    TempStr path = fmt("%s/%s", Str(kIgnoreDir), Str(name));
    remove(path.s);
}

static void TestAutocorrectIgnorer() {
    TestSuite("autocorrect ignorer");
#if GPUI_OS_WINDOWS
    CreateDirectoryA(kIgnoreDir, nullptr);
#else
    mkdir(kIgnoreDir, 0777);
#endif
    RemoveIgnoreFile(".autocorrectignore");
    utassert(WriteIgnoreFile(".gitignore",
                             "# comment\n"
                             "target/\n"
                             "out\n"
                             "*.log\n"
                             "/rooted.txt\n"
                             "docs/build/\n"
                             "!keep.log\n"));
    utassert(WriteIgnoreFile(".autocorrectignore", "extra/\nkeep.log\n"));

    autocorrect::Ignorer ig;
    autocorrect::IgnorerInit(&ig, Str(kIgnoreDir));

    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("target")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("target/debug/foo")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("out")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("out/rel/editor.exe")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("sub/dir/out")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("foo.log")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("a/b/c.log")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("rooted.txt")));
    utassert(!autocorrect::IgnorerIsIgnored(&ig, StrL("sub/rooted.txt")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("docs/build/page.html")));
    utassert(!autocorrect::IgnorerIsIgnored(&ig, StrL("docs/src/page.html")));
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL("extra/file.txt")));
    // .gitignore is added after .autocorrectignore, so its !keep.log wins.
    utassert(!autocorrect::IgnorerIsIgnored(&ig, StrL("keep.log")));
    utassert(!autocorrect::IgnorerIsIgnored(&ig, StrL("src/main.cpp")));
    utassert(!autocorrect::IgnorerIsIgnored(&ig, StrL(".github")));
    // Windows separators normalize.
    utassert(autocorrect::IgnorerIsIgnored(&ig, StrL(".\\target\\debug")));

    autocorrect::IgnorerFree(&ig);
    RemoveIgnoreFile(".gitignore");
    RemoveIgnoreFile(".autocorrectignore");
#if GPUI_OS_WINDOWS
    RemoveDirectoryA(kIgnoreDir);
#else
    rmdir(kIgnoreDir);
#endif
}

void TestAutocorrectMarkdownFormat(Arena* a);

void TestAutocorrect() {
    gAcArena = base::ArenaNew();
    TestAutocorrectFormat();
    TestAutocorrectWidthRules();
    TestAutocorrectToggle();
    TestAutocorrectTypes();
    TestAutocorrectCode();
    TestAutocorrectMarkdownLint();
    TestAutocorrectMarkdownFormat(gAcArena);
    TestAutocorrectIgnorer();
    ArenaDelete(gAcArena);
    gAcArena = nullptr;
}
