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
- (id)hostAPModeConfigurationAndPassword:(NSString **)password;
@end

@interface CWFXPCRequestProxy : NSObject
- (void)__startNetworkRelayBridgeWithHostAPConfiguration:(id)configuration
                                           interfaceName:(NSString *)interfaceName
                                                   reply:(void (^)(NSError *))reply;
- (void)__stopNetworkRelayBridgeForInterfaceName:(NSString *)interfaceName
                                            reply:(void (^)(NSError *))reply;
@end

@interface CWFHostAPConfiguration : NSObject
- (void)setSSID:(NSData *)SSID;
- (void)setChannel:(id)channel;
- (void)setSecurityType:(NSUInteger)securityType;
- (void)setPassword:(NSString *)password;
- (void)setBridgeInterfaceName:(NSString *)interfaceName;
- (void)setBridgeMode:(int)mode;
- (void)setBridgeType:(int)type;
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

static BOOL
method_name_is_relevant(const char *name)
{
    if (name == NULL)
        return NO;
    NSString *lower = [[NSString stringWithUTF8String:name] lowercaseString];
    return [lower containsString:@"hostap"] ||
        [lower containsString:@"networkrelay"] ||
        [lower containsString:@"internetsharing"] ||
        [lower containsString:@"netrb"];
}

static void
print_relevant_methods_for_class(Class cls, BOOL classMethods)
{
    Class methodClass = classMethods ? object_getClass(cls) : cls;
    unsigned int methodCount = 0;
    Method *methods = class_copyMethodList(methodClass, &methodCount);
    for (unsigned int index = 0; index < methodCount; index++) {
        SEL selector = method_getName(methods[index]);
        const char *name = sel_getName(selector);
        if (!method_name_is_relevant(name))
            continue;
        const char *encoding = method_getTypeEncoding(methods[index]);
        printf("runtime class=%s kind=%c selector=%s encoding=%s\n",
               class_getName(cls), classMethods ? '+' : '-', name,
               encoding != NULL ? encoding : "<null>");
    }
    free(methods);
}

static void
print_class_layout(Class cls)
{
    for (Class current = cls; current != Nil;
         current = class_getSuperclass(current)) {
        printf("layout class=%s superclass=%s size=%zu\n",
               class_getName(current),
               class_getSuperclass(current) != Nil
                   ? class_getName(class_getSuperclass(current))
                   : "<none>",
               class_getInstanceSize(current));
        unsigned int ivarCount = 0;
        Ivar *ivars = class_copyIvarList(current, &ivarCount);
        for (unsigned int index = 0; index < ivarCount; index++) {
            printf("ivar class=%s name=%s offset=%td encoding=%s\n",
                   class_getName(current), ivar_getName(ivars[index]),
                   ivar_getOffset(ivars[index]),
                   ivar_getTypeEncoding(ivars[index]));
        }
        free(ivars);

        for (unsigned int kind = 0; kind < 2; kind++) {
            Class methodClass = kind != 0 ? object_getClass(current) : current;
            unsigned int methodCount = 0;
            Method *methods = class_copyMethodList(methodClass, &methodCount);
            for (unsigned int index = 0; index < methodCount; index++) {
                printf("method class=%s kind=%c selector=%s encoding=%s\n",
                       class_getName(current), kind != 0 ? '+' : '-',
                       sel_getName(method_getName(methods[index])),
                       method_getTypeEncoding(methods[index]));
            }
            free(methods);
        }
    }
}

static void
print_relevant_runtime_methods(void)
{
    int classCount = objc_getClassList(NULL, 0);
    if (classCount <= 0)
        return;
    Class *classes = (Class *)calloc((size_t)classCount, sizeof(*classes));
    if (classes == NULL)
        return;
    classCount = objc_getClassList(classes, classCount);
    for (int index = 0; index < classCount; index++) {
        const char *name = class_getName(classes[index]);
        if (name == NULL ||
            (strncmp(name, "CW", 2) != 0 &&
             strncmp(name, "CWF", 3) != 0))
            continue;
        print_relevant_methods_for_class(classes[index], NO);
        print_relevant_methods_for_class(classes[index], YES);
    }
    free(classes);
}

static BOOL
parse_security_type(const char *value, NSUInteger *securityType)
{
    if (strcmp(value, "open") == 0) {
        *securityType = 2;
        return YES;
    }
    if (strcmp(value, "wpa2") == 0) {
        *securityType = 0x80;
        return YES;
    }
    if (strcmp(value, "wpa3") == 0) {
        *securityType = 0x1000;
        return YES;
    }

    char *end = NULL;
    const unsigned long long parsed = strtoull(value, &end, 0);
    if (end == value || *end != '\0')
        return NO;
    *securityType = (NSUInteger)parsed;
    return YES;
}

int
main(int argc, const char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

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

        if (argc > 2 && strcmp(argv[2], "--inspect-runtime") == 0) {
            print_relevant_runtime_methods();
            return 0;
        }
        if (argc > 3 && strcmp(argv[2], "--inspect-class") == 0) {
            Class cls = NSClassFromString(
                [NSString stringWithUTF8String:argv[3]]);
            if (cls == Nil) {
                fprintf(stderr, "runtime class %s not found\n", argv[3]);
                return 1;
            }
            print_class_layout(cls);
            return 0;
        }
        if (argc > 2 && strcmp(argv[2], "--inspect-current") == 0) {
            NSString *password = nil;
            id configuration =
                [interface hostAPModeConfigurationAndPassword:&password];
            printf("configuration=%s has-password=%u\n",
                   configuration != nil
                       ? [[configuration description] UTF8String]
                       : "<none>",
                   password != nil ? 1U : 0U);
            return 0;
        }
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
        const BOOL startSharing =
            strcmp(argv[2], "--start-sharing") == 0;
        if ((!startSharing && strcmp(argv[2], "--start") != 0) || argc < 7) {
            fprintf(stderr,
                    "usage: %s interface --start|--start-sharing ssid "
                    "open|wpa2|wpa3|security-number channel "
                    "password [hold-seconds [upstream-interface "
                    "[relay-interface]]]\n",
                    argv[0]);
            return 2;
        }
        if (![interface respondsToSelector:startSelector]) {
            fprintf(stderr, "startHostAPMode selector is unavailable\n");
            return 1;
        }

        NSData *ssid = [[NSData alloc]
            initWithBytes:argv[3] length:strlen(argv[3])];
        NSUInteger security = 0;
        if (!parse_security_type(argv[4], &security)) {
            fprintf(stderr, "invalid security type %s\n", argv[4]);
            return 2;
        }
        const NSInteger requestedChannel =
            (NSInteger)strtoll(argv[5], NULL, 0);
        NSString *password = strlen(argv[6]) != 0
            ? [NSString stringWithUTF8String:argv[6]]
            : nil;
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

        NSString *actualPassword = nil;
        CWInterface *apInterface = [CWInterface interfaceWithName:@"ap1"];
        id configuration = apInterface != nil
            ? [apInterface
                hostAPModeConfigurationAndPassword:&actualPassword]
            : nil;
        printf("ap-interface=%s configuration=%s has-password=%u\n",
               apInterface != nil ? "ap1" : "<none>",
               configuration != nil
                   ? [[configuration description] UTF8String]
                   : "<none>",
               actualPassword != nil ? 1U : 0U);
        if (configuration == nil) {
            configuration =
                [interface hostAPModeConfigurationAndPassword:&actualPassword];
        }
        if (startSharing && configuration == nil) {
            Class configurationClass =
                NSClassFromString(@"CWFHostAPConfiguration");
            if (configurationClass != Nil) {
                CWFHostAPConfiguration *relayConfiguration =
                    [[configurationClass alloc] init];
                NSString *upstreamInterface = argc > 8
                    ? [NSString stringWithUTF8String:argv[8]]
                    : @"en0";
                [relayConfiguration setSSID:ssid];
                [relayConfiguration setChannel:channel];
                [relayConfiguration setSecurityType:security];
                [relayConfiguration setPassword:password];
                [relayConfiguration
                    setBridgeInterfaceName:upstreamInterface];
                [relayConfiguration setBridgeMode:201];
                [relayConfiguration setBridgeType:301];
                configuration = relayConfiguration;
            }
        }
        if (startSharing && configuration != nil) {
            NSString *upstreamInterface = argc > 8
                ? [NSString stringWithUTF8String:argv[8]]
                : @"en0";
            [(CWFHostAPConfiguration *)configuration
                setBridgeInterfaceName:upstreamInterface];
            [(CWFHostAPConfiguration *)configuration setChannel:channel];
            [(CWFHostAPConfiguration *)configuration setBridgeMode:201];
            [(CWFHostAPConfiguration *)configuration setBridgeType:301];
        }
        printf("configuration=%s has-password=%u\n",
               configuration != nil
                   ? [[configuration description] UTF8String]
                   : "<none>",
               actualPassword != nil ? 1U : 0U);

        __block BOOL relayReplyReceived = NO;
        __block NSError *relayError = nil;
        CWFXPCRequestProxy *relayProxy = nil;
        NSString *relayInterfaceName = argc > 9
            ? [NSString stringWithUTF8String:argv[9]]
            : @"ap1";
        if (startSharing) {
            Class proxyClass = NSClassFromString(@"CWFXPCRequestProxy");
            SEL relaySelector = sel_registerName(
                "__startNetworkRelayBridgeWithHostAPConfiguration:"
                "interfaceName:reply:");
            if (configuration == nil || proxyClass == Nil ||
                ![proxyClass instancesRespondToSelector:relaySelector]) {
                fprintf(stderr, "NetworkRelay producer is unavailable\n");
                [interface stopHostAPMode];
                return 1;
            }
            relayProxy = [[proxyClass alloc] init];
            [relayProxy
                __startNetworkRelayBridgeWithHostAPConfiguration:configuration
                interfaceName:relayInterfaceName
                reply:^(NSError *error) {
                    relayError = error;
                    relayReplyReceived = YES;
                    printf("NetworkRelay start reply error=%s\n",
                           error != nil
                               ? [[error description] UTF8String]
                               : "<none>");
                    fflush(stdout);
                }];
            const NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:10.0];
            while (!relayReplyReceived &&
                   [deadline timeIntervalSinceNow] > 0.0) {
                [[NSRunLoop currentRunLoop]
                    runMode:NSDefaultRunLoopMode
                    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
            }
            printf("NetworkRelay start reply-received=%u error=%s\n",
                   relayReplyReceived ? 1U : 0U,
                   relayError != nil
                       ? [[relayError description] UTF8String]
                       : "<none>");
            if (relayReplyReceived && relayError != nil) {
                [interface stopHostAPMode];
                return 1;
            }
        }

        const unsigned long holdSeconds =
            argc > 7 ? strtoul(argv[7], NULL, 0) : 0;
        if (holdSeconds != 0) {
            printf("holding CoreWLAN client for %lu seconds\n", holdSeconds);
            sleep((unsigned int)holdSeconds);
        }
        if (startSharing) {
            __block BOOL stopReplyReceived = NO;
            [relayProxy
                __stopNetworkRelayBridgeForInterfaceName:
                    relayInterfaceName
                reply:^(NSError *error) {
                    printf("NetworkRelay stop reply error=%s\n",
                           error != nil
                               ? [[error description] UTF8String]
                               : "<none>");
                    stopReplyReceived = YES;
                    fflush(stdout);
                }];
            const NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
            while (!stopReplyReceived &&
                   [deadline timeIntervalSinceNow] > 0.0) {
                [[NSRunLoop currentRunLoop]
                    runMode:NSDefaultRunLoopMode
                    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
            }
            [interface stopHostAPMode];
        }
    }
    return 0;
}
