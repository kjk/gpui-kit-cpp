/* crates/story/src/fixtures: the JSON chart_story.rs loads at startup.
   An example here has no JSON reader and no file to read, so the four
   fixtures that story uses are transcribed, values and all. */

// daily-devices.json: 91 days of device counts.
static const int kDailyDeviceCount = 91;
static const char* const kDailyDate[] = {
    "Apr 1",  "Apr 2",  "Apr 3",  "Apr 4",  "Apr 5",  "Apr 6",  "Apr 7",
    "Apr 8",  "Apr 9",  "Apr 10", "Apr 11", "Apr 12", "Apr 13", "Apr 14",
    "Apr 15", "Apr 16", "Apr 17", "Apr 18", "Apr 19", "Apr 20", "Apr 21",
    "Apr 22", "Apr 23", "Apr 24", "Apr 25", "Apr 26", "Apr 27", "Apr 28",
    "Apr 29", "Apr 30", "May 1",  "May 2",  "May 3",  "May 4",  "May 5",
    "May 6",  "May 7",  "May 8",  "May 9",  "May 10", "May 11", "May 12",
    "May 13", "May 14", "May 15", "May 16", "May 17", "May 18", "May 19",
    "May 20", "May 21", "May 22", "May 23", "May 24", "May 25", "May 26",
    "May 27", "May 28", "May 29", "May 30", "May 31", "Jun 1",  "Jun 2",
    "Jun 3",  "Jun 4",  "Jun 5",  "Jun 6",  "Jun 7",  "Jun 8",  "Jun 9",
    "Jun 10", "Jun 11", "Jun 12", "Jun 13", "Jun 14", "Jun 15", "Jun 16",
    "Jun 17", "Jun 18", "Jun 19", "Jun 20", "Jun 21", "Jun 22", "Jun 23",
    "Jun 24", "Jun 25", "Jun 26", "Jun 27", "Jun 28", "Jun 29", "Jun 30"};
static const float kDailyDesktop[] = {
    222.f, 97.f,  167.f, 242.f, 373.f, 301.f, 245.f, 409.f, 59.f,  261.f, 327.f,
    292.f, 342.f, 137.f, 120.f, 138.f, 446.f, 364.f, 243.f, 89.f,  137.f, 224.f,
    138.f, 387.f, 215.f, 75.f,  383.f, 122.f, 315.f, 454.f, 165.f, 293.f, 247.f,
    385.f, 481.f, 498.f, 388.f, 149.f, 227.f, 293.f, 335.f, 197.f, 197.f, 448.f,
    473.f, 338.f, 499.f, 315.f, 235.f, 177.f, 82.f,  81.f,  252.f, 294.f, 201.f,
    213.f, 420.f, 233.f, 78.f,  340.f, 178.f, 178.f, 470.f, 103.f, 439.f, 88.f,
    294.f, 323.f, 385.f, 438.f, 155.f, 92.f,  492.f, 81.f,  426.f, 307.f, 371.f,
    475.f, 107.f, 341.f, 408.f, 169.f, 317.f, 480.f, 132.f, 141.f, 434.f, 448.f,
    149.f, 103.f, 446.f};
static const float kDailyMobile[] = {
    111.f, 48.f,  84.f,  121.f, 187.f, 151.f, 123.f, 205.f, 30.f,  131.f, 164.f,
    146.f, 171.f, 69.f,  60.f,  69.f,  223.f, 182.f, 122.f, 44.f,  69.f,  112.f,
    69.f,  194.f, 108.f, 38.f,  192.f, 61.f,  158.f, 227.f, 82.f,  146.f, 124.f,
    192.f, 241.f, 249.f, 194.f, 74.f,  114.f, 146.f, 168.f, 98.f,  98.f,  224.f,
    236.f, 169.f, 250.f, 158.f, 118.f, 88.f,  41.f,  41.f,  126.f, 147.f, 100.f,
    106.f, 210.f, 116.f, 39.f,  170.f, 89.f,  89.f,  235.f, 52.f,  220.f, 44.f,
    147.f, 162.f, 192.f, 219.f, 78.f,  46.f,  246.f, 41.f,  213.f, 154.f, 186.f,
    238.f, 54.f,  171.f, 204.f, 84.f,  158.f, 240.f, 66.f,  70.f,  217.f, 224.f,
    74.f,  52.f,  223.f};
static const float kDailyTablet[] = {
    67.f,  29.f,  50.f,  73.f,  112.f, 91.f,  74.f,  123.f, 18.f,  79.f,  98.f,
    88.f,  103.f, 41.f,  36.f,  41.f,  134.f, 109.f, 73.f,  26.f,  41.f,  67.f,
    41.f,  116.f, 65.f,  23.f,  115.f, 37.f,  95.f,  136.f, 49.f,  88.f,  74.f,
    115.f, 145.f, 149.f, 116.f, 44.f,  68.f,  88.f,  101.f, 59.f,  59.f,  134.f,
    142.f, 101.f, 150.f, 95.f,  71.f,  53.f,  25.f,  25.f,  76.f,  88.f,  60.f,
    64.f,  126.f, 70.f,  23.f,  102.f, 53.f,  53.f,  141.f, 31.f,  132.f, 26.f,
    88.f,  97.f,  115.f, 131.f, 47.f,  28.f,  148.f, 25.f,  128.f, 92.f,  112.f,
    143.f, 32.f,  103.f, 122.f, 50.f,  95.f,  144.f, 40.f,  42.f,  130.f, 134.f,
    44.f,  31.f,  134.f};
static const float kDailyWatch[] = {
    28.f, 12.f, 21.f, 30.f, 47.f, 38.f, 31.f, 51.f, 8.f,  33.f, 41.f, 36.f,
    43.f, 17.f, 15.f, 17.f, 56.f, 46.f, 30.f, 11.f, 17.f, 28.f, 17.f, 48.f,
    27.f, 10.f, 48.f, 15.f, 40.f, 57.f, 20.f, 36.f, 31.f, 48.f, 60.f, 62.f,
    48.f, 18.f, 28.f, 36.f, 42.f, 24.f, 24.f, 56.f, 59.f, 42.f, 62.f, 40.f,
    30.f, 22.f, 10.f, 10.f, 32.f, 37.f, 25.f, 26.f, 52.f, 29.f, 10.f, 42.f,
    22.f, 22.f, 59.f, 13.f, 55.f, 11.f, 37.f, 40.f, 48.f, 55.f, 20.f, 12.f,
    62.f, 10.f, 53.f, 38.f, 46.f, 60.f, 14.f, 43.f, 51.f, 21.f, 40.f, 60.f,
    16.f, 18.f, 54.f, 56.f, 18.f, 13.f, 56.f};

// monthly-devices.json: six months, each with its own color alpha.
static const int kMonthlyDeviceCount = 6;
static const char* const kMonthlyMonth[] = {"Jan",   "Feb", "March",
                                            "April", "May", "June"};
static const float kMonthlyDesktop[] = {186.f, 305.f, 237.f,
                                        73.f,  209.f, 214.f};
static const float kMonthlyAlpha[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f};

// radar-devices.json.
static const int kRadarDeviceCount = 6;
static const char* const kRadarMonth[] = {"January", "February", "March",
                                          "April",   "May",      "June"};
static const float kRadarDesktop[] = {186.f, 305.f, 237.f, 73.f, 209.f, 214.f};
static const float kRadarMobile[] = {80.f, 200.f, 120.f, 190.f, 130.f, 140.f};

// stock-prices.json.
static const int kStockPriceCount = 6;
static const char* const kStockDate[] = {"Jan", "Feb", "Mar",
                                         "Apr", "May", "Jun"};
static const float kStockOpen[] = {100.f, 110.f, 111.f, 116.f, 110.f, 115.f};
static const float kStockHigh[] = {112.f, 112.f, 118.f, 120.f, 118.f, 125.f};
static const float kStockLow[] = {95.f, 108.f, 110.f, 108.f, 105.f, 113.f};
static const float kStockClose[] = {110.f, 111.f, 116.f, 110.f, 115.f, 123.f};

// monthly-metrics.json: one SaaS year, 2025.
static const int kMetricCount = 12;
static const char* const kMetricMonth[] = {"Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug",
                                           "Sep", "Oct", "Nov", "Dec"};
static const float kMetricRevenue[] = {38400, 40100, 43600, 46000,
                                       47200, 47200, 45700, 47900,
                                       52200, 54800, 57700, 61500};
static const float kMetricLastYear[] = {31900, 32800, 33600, 34900,
                                        35200, 34100, 33500, 34400,
                                        36100, 37800, 39200, 41000};
static const float kMetricExpenses[] = {29400, 35700, 35300, 41900,
                                        41700, 48100, 36700, 48500,
                                        44900, 42400, 43800, 45500};
static const float kMetricMrr[] = {39206, 40124, 41932, 43389, 44912, 46249,
                                   47094, 48372, 49234, 50233, 51547, 53464};
static const float kMetricSignups[] = {437, 384, 409, 456, 493, 394,
                                       438, 411, 462, 439, 524, 443};
static const float kMetricOrders[] = {1479, 1672, 1585, 1760, 1812, 1703,
                                      1461, 1670, 1883, 1703, 2014, 1787};
static const float kMetricRefunds[] = {49, 73, 44, 69, 49, 68,
                                       56, 54, 76, 55, 73, 43};
static const float kMetricConversion[] = {2.33f, 2.56f, 2.74f, 2.65f,
                                          3.0f,  3.01f, 2.84f, 2.89f,
                                          3.3f,  3.25f, 3.42f, 3.31f};
static const float kMetricSubscriptions[] = {
    1208, 1270, 1306, 1360, 1392, 1438, 1499, 1542, 1584, 1625, 1679, 1707};
static const float kMetricActiveUsers[] = {8921,  9264,  9385,  9581,
                                           9824,  10127, 10530, 10676,
                                           11009, 11212, 11627, 11779};
static const float kMetricSessions[] = {29237, 37784, 30855, 36687,
                                        34461, 39670, 40487, 37920,
                                        43989, 41273, 41381, 38768};
static const float kMetricStorage[] = {2.1f, 2.5f, 2.5f, 2.5f, 2.5f, 2.9f,
                                       3.3f, 3.3f, 3.6f, 3.6f, 4.0f, 4.0f};
static const float kMetricDeploys[] = {19, 19, 29, 23, 42, 36,
                                       27, 30, 39, 31, 30, 35};
static const float kMetricDownloads[] = {2051, 1819, 2401, 2082, 2595, 1872,
                                         2364, 2401, 2638, 2434, 2084, 2360};

// traffic-sources.json / browsers.json / plans.json / regions.json /
// products.json / pages.json / product-scores.json.
static const int kTrafficCount = 5;
static const char* const kTrafficSource[] = {"Direct", "Organic Search",
                                             "Social", "Referral", "Email"};
static const float kTrafficVisitors[] = {4820, 6930, 2140, 1580, 960};

static const int kBrowserCount = 5;
static const char* const kBrowserName[] = {"Chrome", "Safari", "Edge",
                                           "Firefox", "Other"};
static const float kBrowserShare[] = {63.4f, 19.8f, 7.6f, 5.9f, 3.3f};

static const int kPlanCount = 4;
static const char* const kPlanName[] = {"Free", "Starter", "Pro", "Enterprise"};
static const float kPlanAccounts[] = {6420, 1830, 940, 120};

static const int kRegionCount = 5;
static const char* const kRegionName[] = {"N. America", "Europe", "APAC",
                                          "LatAm", "Middle East"};
static const float kRegionRevenue[] = {128400, 96200, 71500, 23800, 14100};

static const int kProductCount = 6;
static const char* const kProductName[] = {"Pro plan",         "Team plan",
                                           "Starter plan",     "Add-on storage",
                                           "Priority support", "Training"};
static const float kProductSales[] = {3240, 2410, 1980, 1120, 640, 310};

static const int kPageCount = 5;
static const char* const kPageName[] = {"/pricing", "/docs", "/blog",
                                        "/changelog", "/about"};
static const float kPageViews[] = {18400, 15200, 9800, 6100, 2900};

static const int kScoreCount = 6;
static const char* const kScoreDim[] = {
    "Performance", "Reliability", "Security", "Usability", "Docs", "Support"};
static const float kScoreAlpha[] = {88, 81, 76, 92, 70, 64};
static const float kScoreBeta[] = {72, 90, 84, 68, 79, 86};

// A node whose fixture row had no growth figure.
static const float kTslaNoGrowth = 1e30f;

// tsla-income-statement.json: two fiscal years of a TSLA income
// statement, as the sankey the Rust story draws twice.
struct TslaNode {
    const char* name;
    double value;
    // The year-over-year change, or NaN where the fixture has none.
    float growth;
    Rgba color;
};
struct TslaLink {
    int source;
    int target;
    double value;
};

static const TslaNode kTsla0Nodes[] = {
    {"Automotive", 82056000000.0, -6.33304f, Rgb(0x5A, 0x74, 0xFF)},
    {"Energy Generation and Storage", 12771000000.0, 26.62106f,
     Rgb(0x5A, 0x74, 0xFF)},
    {"Total Revenue", 94827000000.0, -2.93070f, Rgb(0x5A, 0x74, 0xFF)},
    {"Gross Profit", 17094000000.0, -2.04011f, Rgb(0x35, 0xC1, 0x5C)},
    {"Cost Of Revenues", 77733000000.0, -3.12438f, Rgb(0xFF, 0xA6, 0x1F)},
    {"Other", 862000000.0, kTslaNoGrowth, Rgb(0x35, 0xC1, 0x5C)},
    {"Operating Income", 4355000000.0, -43.13879f, Rgb(0x35, 0xC1, 0x5C)},
    {"Operating Exp.", 12739000000.0, 30.10928f, Rgb(0xFF, 0xA6, 0x1F)},
    {"Net Income", 3794000000.0, -46.49556f, Rgb(0x35, 0xC1, 0x5C)},
    {"Income Tax Exp.", 1423000000.0, -22.53674f, Rgb(0xFF, 0xA6, 0x1F)},
    {"SG& A Exp.", 5834000000.0, 13.28155f, Rgb(0xFF, 0xA6, 0x1F)},
    {"R&D Exp.", 6411000000.0, 41.21145f, Rgb(0xFF, 0xA6, 0x1F)},
    {"Other", 494000000.0, kTslaNoGrowth, Rgb(0xFF, 0xA6, 0x1F)},
};
static const TslaLink kTsla0Links[] = {
    {0, 2, 82056000000.0}, {1, 2, 12771000000.0}, {2, 3, 17094000000.0},
    {2, 4, 77733000000.0}, {3, 6, 4355000000.0},  {3, 7, 12739000000.0},
    {5, 8, 862000000.0},   {6, 8, 2932000000.0},  {6, 9, 1423000000.0},
    {7, 10, 5834000000.0}, {7, 11, 6411000000.0}, {7, 12, 494000000.0},
};
static const TslaNode kTsla1Nodes[] = {
    {"Automotive", 87604000000.0, -3.45390f, Rgb(0x5A, 0x74, 0xFF)},
    {"Energy Generation and Storage", 10086000000.0, 67.12510f,
     Rgb(0x5A, 0x74, 0xFF)},
    {"Total Revenue", 97690000000.0, 0.94758f, Rgb(0x5A, 0x74, 0xFF)},
    {"Gross Profit", 17450000000.0, -1.18913f, Rgb(0x35, 0xC1, 0x5C)},
    {"Cost Of Revenues", 80240000000.0, 1.42454f, Rgb(0xFF, 0xA6, 0x1F)},
    {"Other", 1269000000.0, kTslaNoGrowth, Rgb(0x35, 0xC1, 0x5C)},
    {"Operating Income", 7659000000.0, -13.85671f, Rgb(0x35, 0xC1, 0x5C)},
    {"Operating Exp.", 9791000000.0, 11.65469f, Rgb(0xFF, 0xA6, 0x1F)},
    {"Net Income", 7091000000.0, -52.71721f, Rgb(0x35, 0xC1, 0x5C)},
    {"Income Tax Exp.", 1837000000.0, 136.73265f, Rgb(0xFF, 0xA6, 0x1F)},
    {"SG& A Exp.", 5150000000.0, 7.29167f, Rgb(0xFF, 0xA6, 0x1F)},
    {"R&D Exp.", 4540000000.0, 14.38650f, Rgb(0xFF, 0xA6, 0x1F)},
    {"Other", 101000000.0, kTslaNoGrowth, Rgb(0xFF, 0xA6, 0x1F)},
};
static const TslaLink kTsla1Links[] = {
    {0, 2, 87604000000.0}, {1, 2, 10086000000.0}, {2, 3, 17450000000.0},
    {2, 4, 80240000000.0}, {3, 6, 7659000000.0},  {3, 7, 9791000000.0},
    {5, 8, 1269000000.0},  {6, 8, 5822000000.0},  {6, 9, 1837000000.0},
    {7, 10, 5150000000.0}, {7, 11, 4540000000.0}, {7, 12, 101000000.0},
};
static const char* const kTslaPeriods[] = {"FY 2025", "FY 2024"};
static const int kTslaStatementCount = 2;
static const int kTslaNodeCount = 13;
static const int kTslaLinkCount = 12;
