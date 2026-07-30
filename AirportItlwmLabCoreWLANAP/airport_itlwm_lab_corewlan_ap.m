#import <CoreWLAN/CoreWLAN.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

@interface CWInterface (AirportItlwmLabHostAP)
- (BOOL)startHostAPModeWithSSID:(NSData *)ssid
                  securityType:(NSUInteger)securityType
                       channel:(CWChannel *)channel
                      password:(NSString *)password
                         error:(NSError **)error;
- (void)stopHostAPMode;
@end

static void
print_method_signature(id object, SEL selector)
{
    Method method = class_getInstanceMethod([object class], selector);
    if (method == NULL) {
        fprintf(stderr, "selector %s is not implemented by %s\n",
                sel_getName(selector), class_getName([object class]));
        return;
    }

    const char *encoding = method_getTypeEncoding(method);
    printf("class=%s selector=%s encoding=%s\n",
           class_getName([object class]), sel_getName(selector),
           encoding != NULL ? encoding : "<null>");

    NSMethodSignature *signature =
        [object methodSignatureForSelector:selector];
    if (signature == nil)
        return;
    printf("return=%s argc=%lu",
           [signature methodReturnType],
           (unsigned long)[signature numberOfArguments]);
    for (NSUInteger index = 0;
         index < [signature numberOfArguments]; index++) {
        printf(" arg%lu=%s", (unsigned long)index,
               [signature getArgumentTypeAtIndex:index]);
    }
    printf("\n");
}

int
main(int argc, const char *argv[])
{
    @autoreleasepool {
        const char *interfaceName = argc > 1 ? argv[1] : "en1";
        CWInterface *interface =
            [CWInterface interfaceWithName:
                [NSString stringWithUTF8String:interfaceName]];
        if (interface == nil) {
            fprintf(stderr, "CoreWLAN interface %s not found\n",
                    interfaceName);
            return 1;
        }

        SEL startSelector = sel_registerName(
            "startHostAPModeWithSSID:securityType:channel:password:error:");
        SEL stopSelector = sel_registerName("stopHostAPMode");
        SEL configurationSelector = sel_registerName(
            "hostAPModeConfigurationAndPassword:");
        print_method_signature(interface, startSelector);
        print_method_signature(interface, stopSelector);
        print_method_signature(interface, configurationSelector);

        if (argc <= 2 || strcmp(argv[2], "--inspect") == 0)
            return 0;
        if (strcmp(argv[2], "--stop") == 0) {
            if (![interface respondsToSelector:stopSelector]) {
                fprintf(stderr, "stopHostAPMode is unavailable\n");
                return 1;
            }
            [interface stopHostAPMode];
            printf("stopHostAPMode sent\n");
            return 0;
        }
        if (strcmp(argv[2], "--start") != 0 || argc < 7) {
            fprintf(stderr,
                    "usage: %s interface --start ssid security channel "
                    "password [hold-seconds]\n",
                    argv[0]);
            return 2;
        }
        if (![interface respondsToSelector:startSelector]) {
            fprintf(stderr, "startHostAPMode selector is unavailable\n");
            return 1;
        }

        NSData *ssid = [[NSData alloc]
            initWithBytes:argv[3] length:strlen(argv[3])];
        const NSUInteger security =
            (NSUInteger)strtoull(argv[4], NULL, 0);
        const NSInteger requestedChannel =
            (NSInteger)strtoll(argv[5], NULL, 0);
        NSString *password = [NSString stringWithUTF8String:argv[6]];
        CWChannel *channel = nil;
        for (CWChannel *candidate in [interface supportedWLANChannels]) {
            if ([candidate channelNumber] == requestedChannel) {
                channel = candidate;
                break;
            }
        }
        if (channel == nil) {
            fprintf(stderr, "channel %ld is not in supportedWLANChannels\n",
                    (long)requestedChannel);
            return 2;
        }

        NSError *error = nil;
        const BOOL started =
            [interface startHostAPModeWithSSID:ssid
                                 securityType:security
                                      channel:channel
                                     password:password
                                        error:&error];
        printf("start result=%u error=%s\n", started ? 1U : 0U,
               error != nil
                   ? [[error description] UTF8String]
                   : "<none>");
        if (!started)
            return 1;

        const unsigned long holdSeconds =
            argc > 7 ? strtoul(argv[7], NULL, 0) : 0;
        if (holdSeconds != 0) {
            printf("holding CoreWLAN client for %lu seconds\n", holdSeconds);
            sleep((unsigned int)holdSeconds);
        }
    }
    return 0;
}
