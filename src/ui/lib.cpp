#include "ui/lib.h"

namespace gpui {
namespace component {

void Init(App* app) {
    if (!app) {
        return;
    }
    ThemeRegistryInit(app);
    UiGlobalStateInit(app);
    BaseInit(app);
    ThemeSyncBase(app);

    DatePickerInitKeys();
    CarouselInitKeys();
    ListInitKeys();
    CommandInitKeys();
    NotificationInitSystem(app);
    PopupMenuInitKeys();
    TableInitKeys();
    TextViewInitKeys();
    SelectInitKeys();
}

} // namespace component
} // namespace gpui
