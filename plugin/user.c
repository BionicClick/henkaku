#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/io/fcntl.h>
#include <taihen.h>
#include "henkaku.h"
#include "../build/version.c"

#define DISPLAY_VERSION (0x3600000)

extern unsigned char _binary_system_settings_xml_start;
extern unsigned char _binary_system_settings_xml_size;
extern unsigned char _binary_henkaku_settings_xml_start;
extern unsigned char _binary_henkaku_settings_xml_size;

static henkaku_config_t config;

static SceUID g_hooks[12];

static tai_hook_ref_t g_sceKernelGetSystemSwVersion_SceSettings_hook;
static int sceKernelGetSystemSwVersion_SceSettings_patched(SceKernelFwInfo *info) {
  int ret;
  int ver_major;
  int ver_minor;
  ret = TAI_CONTINUE(int, g_sceKernelGetSystemSwVersion_SceSettings_hook, info);
  ver_major = ((DISPLAY_VERSION >> 24) & 0xF) + 10 * (DISPLAY_VERSION >> 28);
  ver_minor = ((DISPLAY_VERSION >> 16) & 0xF) + 10 * ((DISPLAY_VERSION >> 20) & 0xF);
  if (BETA_RELEASE) {
    sceClibSnprintf(info->versionString, 16, "%d.%02d \xE5\xA4\x89\xE9\x9D\xA9-%d\xCE\xB2%d", ver_major, ver_minor, HENKAKU_RELEASE, BETA_RELEASE);
  } else {
    sceClibSnprintf(info->versionString, 16, "%d.%02d \xE5\xA4\x89\xE9\x9D\xA9-%d", ver_major, ver_minor, HENKAKU_RELEASE);
  }
  return ret;
}

static tai_hook_ref_t g_update_check_hook;
static int update_check_patched(int a1, int a2, int *a3, int a4, int a5) {
  TAI_CONTINUE(int, g_update_check_hook, a1, a2, a3, a4, a5);
  *a3 = 0;
  return 0;
}

static tai_hook_ref_t g_game_update_check_hook;
static int game_update_check_patched(int newver, int *needsupdate) {
  TAI_CONTINUE(int, g_game_update_check_hook, newver, needsupdate);
  *needsupdate = 0;
  return 0;
}

static tai_hook_ref_t g_passphrase_decrypt_hook;
static void passphrase_decrypt_patched(void *dat0, void *dat1, void *dat2, char *passphrase, int *result) {
  TAI_CONTINUE(void, g_passphrase_decrypt_hook, dat0, dat1, dat2, passphrase, result);
  if (config.use_psn_spoofing && PSN_PASSPHRASE[0] != '\0' && *result == 1) {
    sceClibMemcpy(passphrase, PSN_PASSPHRASE, sizeof(PSN_PASSPHRASE));
  }
}

static int save_config_user(void) {
  SceUID fd;
  int rd;
  sceIoMkdir("ux0:temp", 6);
  sceIoMkdir("ux0:temp/app_work", 6);
  sceIoMkdir("ux0:temp/app_work/MLCL00001", 6);
  sceIoMkdir("ux0:temp/app_work/MLCL00001/rec", 6);
  fd = sceIoOpen(CONFIG_PATH, SCE_O_TRUNC | SCE_O_CREAT | SCE_O_WRONLY, 6);
  if (fd >= 0) {
    rd = sceIoWrite(fd, &config, sizeof(config));
    sceIoClose(fd);
    if (rd != sizeof(config)) {
      LOG("config not right size: %d", rd);
    }
  } else {
    LOG("config file not found");
  }
  return 0;
}

static int load_config_user(void) {
  SceUID fd;
  int rd;
  fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
  if (fd >= 0) {
    rd = sceIoRead(fd, &config, sizeof(config));
    sceIoClose(fd);
    if (rd == sizeof(config)) {
      if (config.magic == HENKAKU_CONFIG_MAGIC) {
        if (config.version >= 8) {
          return 0;
        } else {
          LOG("config version too old");
        }
      } else {
        LOG("config incorrect magic: %x", config.magic);
      }
    } else {
      LOG("config not right size: %d", rd);
    }
  } else {
    LOG("config file not found");
  }
  // default config
  config.magic = HENKAKU_CONFIG_MAGIC;
  config.version = HENKAKU_RELEASE;
  config.use_psn_spoofing = 1;
  config.allow_unsafe_hb = 0;
  config.use_spoofed_version = 1;
  config.spoofed_version = SPOOF_VERSION;
  return 0;
}

static tai_hook_ref_t g_sceRegMgrGetKeyInt_SceSystemSettingsCore_hook;
static int sceRegMgrGetKeyInt_SceSystemSettingsCore_patched(const char *category, const char *name, int *value) {
  if (sceClibStrncmp(category, "/CONFIG/HENKAKU", 15) == 0) {
    if (value) {
      load_config_user();
      if (sceClibStrncmp(name, "enable_psn_spoofing", 19) == 0) {
        *value = config.use_psn_spoofing;
      } else if (sceClibStrncmp(name, "enable_unsafe_homebrew", 22) == 0) {
        *value = config.allow_unsafe_hb;
      } else if (sceClibStrncmp(name, "enable_version_spoofing", 23) == 0) {
        *value = config.use_spoofed_version;
      }
    }
    return 0;
  }

  return TAI_CONTINUE(int, g_sceRegMgrGetKeyInt_SceSystemSettingsCore_hook, category, name, value);
}

static tai_hook_ref_t g_sceRegMgrSetKeyInt_SceSystemSettingsCore_hook;
static int sceRegMgrSetKeyInt_SceSystemSettingsCore_patched(const char *category, const char *name, int value) {
  if (sceClibStrncmp(category, "/CONFIG/HENKAKU", 15) == 0) {
    if (sceClibStrncmp(name, "enable_psn_spoofing", 19) == 0) {
      config.use_psn_spoofing = value;
    } else if (sceClibStrncmp(name, "enable_unsafe_homebrew", 22) == 0) {
      config.allow_unsafe_hb = value;
    } else if (sceClibStrncmp(name, "enable_version_spoofing", 23) == 0) {
      config.use_spoofed_version = value;
    }
    save_config_user();
    henkaku_reload_config();
    return 0;
  }

  return TAI_CONTINUE(int, g_sceRegMgrSetKeyInt_SceSystemSettingsCore_hook, category, name, value);
}

static int build_version_string(int version, char *string, int length) {
  if (version) {
    char a = (version >> 24) & 0xF;
    char b = (version >> 20) & 0xF;
    char c = (version >> 16) & 0xF;
    char d = (version >> 12) & 0xF;
    string[0] = '0' + a;
    string[1] = '.';
    string[2] = '0' + b;
    string[3] = '0' + c;
    string[4] = '\0';
    if (d) {
      string[4] = '0' + d;
      string[5] = '\0';
    }
    return 1;
  }
  return 0;
}

static tai_hook_ref_t g_sceRegMgrGetKeyStr_SceSystemSettingsCore_hook;
static int sceRegMgrGetKeyStr_SceSystemSettingsCore_patched(const char *category, const char *name, char *string, int length) {
  if (sceClibStrncmp(category, "/CONFIG/HENKAKU", 15) == 0) {
    if (sceClibStrncmp(name, "spoofed_version", 15) == 0) {
      if (string != NULL) {
        load_config_user();
        sceClibMemset(string, 0, length);
        if (!build_version_string(config.spoofed_version, string, length))
          build_version_string(SPOOF_VERSION, string, length);
      }
    }
    return 0;
  }
  return TAI_CONTINUE(int, g_sceRegMgrGetKeyStr_SceSystemSettingsCore_hook, category, name, string, length);
}

#define IS_DIGIT(i) (i >= '0' && i <= '9')
static tai_hook_ref_t g_sceRegMgrSetKeyStr_SceSystemSettingsCore_hook;
static int sceRegMgrSetKeyStr_SceSystemSettingsCore_patched(const char *category, const char *name, const char *string, int length) {
  if (sceClibStrncmp(category, "/CONFIG/HENKAKU", 15) == 0) {
    if (sceClibStrncmp(name, "spoofed_version", 15) == 0) {
      if (string != NULL) {
        if (IS_DIGIT(string[0]) && string[1] == '.' && IS_DIGIT(string[2]) && IS_DIGIT(string[3])) {
          char a = string[0] - '0';
          char b = string[2] - '0';
          char c = string[3] - '0';
          char d = IS_DIGIT(string[4]) ? string[4] - '0' : '\0';
          config.spoofed_version = ((a << 24) | (b << 20) | (c << 16) | (d << 12));
          save_config_user();
          henkaku_reload_config();
        }
      }
    }
    return 0;
  }
  return TAI_CONTINUE(int, g_sceRegMgrSetKeyStr_SceSystemSettingsCore_hook, category, name, string, length);
}

typedef struct {
  int size;
  const char *name;
  int type;
  int unk;
} SceRegMgrKeysInfo;

static tai_hook_ref_t g_sceRegMgrGetKeysInfo_SceSystemSettingsCore_hook;
static int sceRegMgrGetKeysInfo_SceSystemSettingsCore_patched(const char *category, SceRegMgrKeysInfo *info, int unk) {
  if (sceClibStrncmp(category, "/CONFIG/HENKAKU", 15) == 0) {
    if (info) {
      if (sceClibStrncmp(info->name, "spoofed_version", 15) == 0) {
        info->type = 0x00030001;
      } else {
        info->type = 0x00040000;
      }
    }

    return 0;
  }
  return TAI_CONTINUE(int, g_sceRegMgrGetKeysInfo_SceSystemSettingsCore_hook, category, info, unk);
}

static tai_hook_ref_t g_ScePafMisc_19FE55A8_SceSettings_hook;
static int ScePafMisc_19FE55A8_SceSettings_patched(int a1, void *xml_buf, int xml_size, int a4) {
  if ((82+22) < xml_size && sceClibStrncmp(xml_buf+82, "system_settings_plugin", 22) == 0) {
    xml_buf = (void *)&_binary_system_settings_xml_start;
    xml_size = (int)&_binary_system_settings_xml_size;
  } else if ((79+19) < xml_size && sceClibStrncmp(xml_buf+79, "idu_settings_plugin", 19) == 0) {
    xml_buf = (void *)&_binary_henkaku_settings_xml_start;
    xml_size = (int)&_binary_henkaku_settings_xml_size;
  }
  return TAI_CONTINUE(int, g_ScePafMisc_19FE55A8_SceSettings_hook, a1, xml_buf, xml_size, a4);
}

static SceUID g_system_settings_core_modid = -1;
static tai_hook_ref_t g_sceKernelLoadStartModule_SceSettings_hook;
SceUID sceKernelLoadStartModule_SceSettings_patched(char *path, SceSize args, void *argp, int flags, SceKernelLMOption *option, int *status) {
  SceUID ret = TAI_CONTINUE(SceUID, g_sceKernelLoadStartModule_SceSettings_hook, path, args, argp, flags, option, status);
  if (ret >= 0 && sceClibStrncmp(path, "vs0:app/NPXS10015/system_settings_core.suprx", 44) == 0) {
    g_system_settings_core_modid = ret;
    g_hooks[6] = taiHookFunctionImport(&g_ScePafMisc_19FE55A8_SceSettings_hook, 
                                        "SceSettings", 
                                        0x3D643CE8, // ScePafMisc
                                        0x19FE55A8, 
                                        ScePafMisc_19FE55A8_SceSettings_patched);
    g_hooks[7] = taiHookFunctionImport(&g_sceRegMgrGetKeyInt_SceSystemSettingsCore_hook, 
                                        "SceSystemSettingsCore", 
                                        0xC436F916, // SceRegMgr
                                        0x16DDF3DC, 
                                        sceRegMgrGetKeyInt_SceSystemSettingsCore_patched);
    g_hooks[8] = taiHookFunctionImport(&g_sceRegMgrSetKeyInt_SceSystemSettingsCore_hook, 
                                        "SceSystemSettingsCore", 
                                        0xC436F916, // SceRegMgr
                                        0xD72EA399, 
                                        sceRegMgrSetKeyInt_SceSystemSettingsCore_patched);
    g_hooks[9] = taiHookFunctionImport(&g_sceRegMgrGetKeyStr_SceSystemSettingsCore_hook, 
                                        "SceSystemSettingsCore", 
                                        0xC436F916, // SceRegMgr
                                        0xE188382F, 
                                        sceRegMgrGetKeyStr_SceSystemSettingsCore_patched);
    g_hooks[10] = taiHookFunctionImport(&g_sceRegMgrSetKeyStr_SceSystemSettingsCore_hook, 
                                        "SceSystemSettingsCore", 
                                        0xC436F916, // SceRegMgr
                                        0x41D320C5, 
                                        sceRegMgrSetKeyStr_SceSystemSettingsCore_patched);
    g_hooks[11] = taiHookFunctionImport(&g_sceRegMgrGetKeysInfo_SceSystemSettingsCore_hook, 
                                        "SceSystemSettingsCore", 
                                        0xC436F916, // SceRegMgr
                                        0x58421DD1, 
                                        sceRegMgrGetKeysInfo_SceSystemSettingsCore_patched);
  }
  return ret;
}

static tai_hook_ref_t g_sceKernelStopUnloadModule_SceSettings_hook;
int sceKernelStopUnloadModule_SceSettings_patched(SceUID modid, SceSize args, void *argp, int flags, SceKernelULMOption *option, int *status) {
  if (modid == g_system_settings_core_modid) {
    g_system_settings_core_modid = -1;
    if (g_hooks[6] >= 0) taiHookRelease(g_hooks[6], g_ScePafMisc_19FE55A8_SceSettings_hook);
    if (g_hooks[7] >= 0) taiHookRelease(g_hooks[7], g_sceRegMgrGetKeyInt_SceSystemSettingsCore_hook);
    if (g_hooks[8] >= 0) taiHookRelease(g_hooks[8], g_sceRegMgrSetKeyInt_SceSystemSettingsCore_hook);
    if (g_hooks[9] >= 0) taiHookRelease(g_hooks[9], g_sceRegMgrGetKeyStr_SceSystemSettingsCore_hook);
    if (g_hooks[10] >= 0) taiHookRelease(g_hooks[10], g_sceRegMgrSetKeyStr_SceSystemSettingsCore_hook);
    if (g_hooks[11] >= 0) taiHookRelease(g_hooks[11], g_sceRegMgrGetKeysInfo_SceSystemSettingsCore_hook);
  }
  return TAI_CONTINUE(int, g_sceKernelStopUnloadModule_SceSettings_hook, modid, args, argp, flags, option, status);
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
  tai_module_info_t info;
  LOG("loading HENkaku config for user");
  load_config_user();
  g_hooks[0] = taiHookFunctionImport(&g_sceKernelLoadStartModule_SceSettings_hook, 
                                      "SceSettings", 
                                      0xCAE9ACE6, // SceLibKernel
                                      0x2DCC4AFA, 
                                      sceKernelLoadStartModule_SceSettings_patched);
  LOG("sceKernelLoadStartModule hook: %x", g_hooks[0]);
  g_hooks[1] = taiHookFunctionImport(&g_sceKernelStopUnloadModule_SceSettings_hook, 
                                      "SceSettings", 
                                      0xCAE9ACE6, // SceLibKernel
                                      0x2415F8A4, 
                                      sceKernelStopUnloadModule_SceSettings_patched);
  LOG("sceKernelStopUnloadModule hook: %x", g_hooks[1]);
  g_hooks[2] = taiHookFunctionImport(&g_sceKernelGetSystemSwVersion_SceSettings_hook, 
                                      "SceSettings", 
                                      0xEAED1616, // SceModulemgr
                                      0x5182E212, 
                                      sceKernelGetSystemSwVersion_SceSettings_patched);
  LOG("sceKernelGetSystemSwVersion hook: %x", g_hooks[2]);
  g_hooks[3] = g_hooks[4] = g_hooks[5] = -1;
  if (config.use_psn_spoofing) {
    info.size = sizeof(info);
    if (taiGetModuleInfo("SceShell", &info) >= 0) {
      // we don't have a nice clean way of doing PSN spoofing (update prompt disable) so 
      // we are stuck with hard coding offsets. Since module NID is different for each 
      // version and retail/dex/test unit, this should allow us to specify different 
      // offsets.
      switch (info.module_nid) {
        case 0x0552F692: { // retail 3.60 SceShell
          g_hooks[3] = taiHookFunctionOffset(&g_update_check_hook, 
                                             info.modid, 
                                             0,         // segidx
                                             0x363de8,  // offset
                                             1,         // thumb
                                             update_check_patched);
          g_hooks[4] = taiHookFunctionOffset(&g_game_update_check_hook, 
                                             info.modid, 
                                             0,         // segidx
                                             0x37beda,  // offset
                                             1,         // thumb
                                             game_update_check_patched);
          g_hooks[5] = taiHookFunctionOffset(&g_passphrase_decrypt_hook, 
                                             info.modid, 
                                             0,         // segidx
                                             0x325230,  // offset
                                             1,         // thumb
                                             passphrase_decrypt_patched);
          break;
        }
        case 0x6CB01295: { // PDEL 3.60 SceShell thanks to anonymous for offsets
          g_hooks[3] = taiHookFunctionOffset(&g_update_check_hook, 
                                             info.modid, 
                                             0,         // segidx
                                             0x12c882,  // offset
                                             1,         // thumb
                                             update_check_patched);
          g_hooks[4] = taiHookFunctionOffset(&g_game_update_check_hook, 
                                             info.modid, 
                                             0,         // segidx
                                             0x36df3e,  // offset
                                             1,         // thumb
                                             game_update_check_patched);
          g_hooks[5] = taiHookFunctionOffset(&g_passphrase_decrypt_hook, 
                                             info.modid, 
                                             0,         // segidx
                                             0x317384,  // offset
                                             1,         // thumb
                                             passphrase_decrypt_patched);
          break;
        }
        default: {
          LOG("SceShell NID %X not recognized, skipping PSN spoofing patches", info.module_nid);
        }
      }
    }
  } else {
    LOG("skipping psn spoofing patches");
  }
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
  LOG("stopping module");
  // free hooks that didn't fail
  if (g_hooks[0] >= 0) taiHookRelease(g_hooks[0], g_sceKernelLoadStartModule_SceSettings_hook);
  if (g_hooks[1] >= 0) taiHookRelease(g_hooks[1], g_sceKernelStopUnloadModule_SceSettings_hook);
  if (g_hooks[2] >= 0) taiHookRelease(g_hooks[2], g_sceKernelGetSystemSwVersion_SceSettings_hook);
  if (g_hooks[3] >= 0) taiHookRelease(g_hooks[3], g_update_check_hook);
  if (g_hooks[4] >= 0) taiHookRelease(g_hooks[4], g_game_update_check_hook);
  if (g_hooks[5] >= 0) taiHookRelease(g_hooks[5], g_passphrase_decrypt_hook);
  return SCE_KERNEL_STOP_SUCCESS;
}
