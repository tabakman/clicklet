// IDI_APPICON must keep the lowest icon id: Explorer shows the lowest-numbered icon group.
#define IDI_APPICON        1
#define IDI_TRAY_ENABLED   2
#define IDI_TRAY_DISABLED  3

#define IDB_ABOUT_LOGO     101
#define IDD_ABOUT          102

#define IDC_ABOUT_LOGO     1001
#define IDC_ABOUT_VERSION  1002
#define IDC_ABOUT_URL      1003

// The only definition of the version; the VERSIONINFO resource and the About dialog derive from it.
#define CLICKLET_VERSION_MAJOR 1
#define CLICKLET_VERSION_MINOR 0
#define CLICKLET_VERSION_PATCH 0

// Two-step stringize, so the macro expands before it is quoted.
#define CLICKLET_STRINGIZE_(x) #x
#define CLICKLET_STRINGIZE(x)  CLICKLET_STRINGIZE_(x)
#define CLICKLET_VERSION_STRING \
    CLICKLET_STRINGIZE(CLICKLET_VERSION_MAJOR) "." \
    CLICKLET_STRINGIZE(CLICKLET_VERSION_MINOR) "." \
    CLICKLET_STRINGIZE(CLICKLET_VERSION_PATCH)
