#include "gpui/gpui.h"

#import <UIKit/UIKit.h>

namespace gpui {

void IosPostAccessibilityAnnouncement(const AccessibilityAnnouncement& event) {
    if (event.priority == AccessibilityLive::Off || !len(event.text)) return;
    NSString* text = [[NSString alloc] initWithBytes:event.text.s
                                              length:(NSUInteger)len(event.text)
                                            encoding:NSUTF8StringEncoding];
    if (!text) return;
    NSNumber* queued = @(event.priority == AccessibilityLive::Polite);
    NSAttributedString* message = [[NSAttributedString alloc]
        initWithString:text
            attributes:@{
                UIAccessibilitySpeechAttributeQueueAnnouncement : queued
            }];
    UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification,
                                    message);
}

} // namespace gpui
