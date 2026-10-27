#include <list>
#include <vector>
#include <string.h>
#include <pthread.h>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include <cmath>
#include <android/log.h>

// Define nosso próprio log em vez de depender da pasta Includes
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ModMenu", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "ModMenu", __VA_ARGS__)

// Vamos manter o OBFUSCATE para as strings
#define OBFUSCATE(str) str 

#include "KittyMemory/MemoryPatch.h"
#include "Menu.h"

#include "Includes/Macros.h"

// ==========================================
// OFFSETS DO JOGO (Atualize a cada patch)
// ==========================================
#define OFF_CURRENT_LOCAL_PLAYER 0x661bcdc
#define OFF_CURRENT_CAMERA_MANAGER 0x6629d20
#define OFF_GET_POSITION 0x7519f20
#define OFF_SET_EULER_ANGLES 0x751ae5c
#define OFF_SKELETON_MANAGER 0xEC4         // Offset onde fica o gerenciador de ossos no Player
#define OFF_GET_BONE_BY_NAME 0x64fdf30     // Offset da função BODJPJHIMAL
#define OFF_BONE_TRANSFORM 0x8             // Offset do Transform dentro do BFOONIGAIFK

// Variáveis do Menu (Controladas pelo Java)
bool featureHookToggle = false; // Aimbot
bool Health = false;            // ESP
int aimBone = 0;                // 0 = Cabeça, 1 = Peito

// ==========================================
// ESTRUTURAS E MATEMÁTICA
// ==========================================
struct Vector3 {
    float x, y, z;
    Vector3 operator-(const Vector3& v) const { return {x - v.x, y - v.y, z - v.z}; }
};

Vector3 CalculateAngle(Vector3 src, Vector3 dst) {
    Vector3 delta = dst - src;
    float hyp = sqrt(delta.x * delta.x + delta.z * delta.z);
    Vector3 angles;
    angles.x = atan2(delta.y, hyp) * 180.0f / 3.14159265f; // Pitch
    angles.y = atan2(delta.x, delta.z) * 180.0f / 3.14159265f; // Yaw
    angles.z = 0.0f;
    return angles;
}

// ==========================================
// TYPEDEFS (Ponteiros para as funções nativas)
// ==========================================
typedef Vector3(*get_Position_t)(void* transform);
typedef void(*set_EulerAngles_t)(void* transform, Vector3 angles);
typedef void* (*CurrentLocalPlayer_t)();
typedef void* (*CurrentCameraManager_t)();
typedef void* (*il2cpp_class_from_name_t)(const char* image, const char* namespaze, const char* name);
typedef void* (*il2cpp_string_new_t)(const char* str);
typedef void* (*GetBoneByName_t)(void* skeletonManager, void* name);

// Base global
uintptr_t il2cppBase = 0;

// ==========================================
// FUNÇÃO PARA PEGAR A POSIÇÃO DO OSSO
// ==========================================
Vector3 GetBonePosition(uintptr_t playerEntity, const char* boneName) {
    // 1. Pega o SkeletonManager (Offset 0xEC4)
    uintptr_t skeletonManager = *(uintptr_t*)(playerEntity + OFF_SKELETON_MANAGER);
    if (!skeletonManager) return {0,0,0};

    // 2. Resolve as funções
    il2cpp_string_new_t il2cpp_string_new = (il2cpp_string_new_t)dlsym(RTLD_DEFAULT, "il2cpp_string_new");
    if (!il2cpp_string_new) return {0,0,0};

    GetBoneByName_t GetBoneByName = (GetBoneByName_t)(il2cppBase + OFF_GET_BONE_BY_NAME);
    get_Position_t get_Position = (get_Position_t)(il2cppBase + OFF_GET_POSITION);

    // 3. Cria a string na memória IL2CPP (Ex: "Head")
    void* str = il2cpp_string_new(boneName);

    // 4. Chama a função do jogo para pegar o osso (BFOONIGAIFK)
    uintptr_t boneNode = (uintptr_t)GetBoneByName((void*)skeletonManager, str);
    if (!boneNode) return {0,0,0};

    // 5. Pega a Transform do osso (Offset 0x8)
    uintptr_t transform = *(uintptr_t*)(boneNode + OFF_BONE_TRANSFORM);
    if (!transform) return {0,0,0};

    // 6. Pega a posição (X, Y, Z)
    return get_Position((void*)transform);
}

// ==========================================
// THREAD PRINCIPAL (O Loop do Hack)
// ==========================================
void *hack_thread(void *arg) {
    LOGI(OBFUSCATE("pthread created"));

    do {
        sleep(1);
    } while (!isLibraryLoaded(targetLibName));

    LOGI(OBFUSCATE("%s has been loaded"), (const char *) targetLibName);

    il2cppBase = getAbsoluteAddress(targetLibName, 0);

    // Resolve funções base
    il2cpp_class_from_name_t il2cpp_class_from_name = (il2cpp_class_from_name_t)dlsym(RTLD_DEFAULT, "il2cpp_class_from_name");
    CurrentLocalPlayer_t CurrentLocalPlayer = (CurrentLocalPlayer_t)(il2cppBase + OFF_CURRENT_LOCAL_PLAYER);
    CurrentCameraManager_t CurrentCameraManager = (CurrentCameraManager_t)(il2cppBase + OFF_CURRENT_CAMERA_MANAGER);
    set_EulerAngles_t set_EulerAngles = (set_EulerAngles_t)(il2cppBase + OFF_SET_EULER_ANGLES);

    while (true) {
        if (featureHookToggle || Health) {
            
            if (il2cpp_class_from_name) {
                // Acha a classe BaseGame para o Dicionário estático
                uintptr_t BaseGameClass = (uintptr_t)il2cpp_class_from_name("Assembly-CSharp.dll", "GCommon", "BaseGame");
                
                uintptr_t localPlayer = (uintptr_t)CurrentLocalPlayer();
                uintptr_t camManager = (uintptr_t)CurrentCameraManager();

                if (localPlayer && BaseGameClass) {
                    // Posição da SUA cabeça (para o cálculo do ângulo)
                    Vector3 localHeadPos = GetBonePosition(localPlayer, "Head"); 
                    if (localHeadPos.x == 0) {
                        // Fallback para a posição base se não achar o osso
                        get_Position_t get_Position = (get_Position_t)(il2cppBase + OFF_GET_POSITION);
                        localHeadPos = get_Position((void*)localPlayer);
                    }
                    
                    uintptr_t cameraTransform = 0;
                    if (camManager) {
                        cameraTransform = *(uintptr_t*)(camManager + 0x18); // Transform da câmera
                    }

                    // Lê o Dicionário de todas as entidades (sAllEntities)
                    uintptr_t dictPtr = *(uintptr_t*)(BaseGameClass + 0x0); 
                    if (dictPtr) {
                        uintptr_t entries = *(uintptr_t*)(dictPtr + 0x18); // _entries
                        int count = *(int*)(dictPtr + 0x20);               // _count
                        
                        uintptr_t bestTarget = 0;
                        float closestDist = 9999.0f;

                        for (int i = 0; i < count; i++) {
                            uintptr_t entry = entries + (i * 24); 
                            uintptr_t entity = *(uintptr_t*)(entry + 0x10); // O objeto Entity (Player)

                            // Pula se for nulo ou se for o próprio jogador local
                            if (!entity || entity == localPlayer) continue; 

                            get_Position_t get_Position = (get_Position_t)(il2cppBase + OFF_GET_POSITION);
                            Vector3 enemyPos = get_Position((void*)entity);

                            // ==========================================
                            // LÓGICA DO ESP (Aguardando a Matriz da Câmera)
                            // ==========================================
                            if (Health) {
                                // TODO: Quando achar o WorldToScreenPoint (Matrix), desenhe aqui
                            }

                            // ==========================================
                            // LÓGICA DO AIMBOT
                            // ==========================================
                            if (featureHookToggle) {
                                float dist = sqrt(pow(enemyPos.x - localHeadPos.x, 2) + pow(enemyPos.y - localHeadPos.y, 2) + pow(enemyPos.z - localHeadPos.z, 2));
                                
                                // Pega o inimigo mais próximo
                                if (dist < closestDist) {
                                    closestDist = dist;
                                    bestTarget = entity;
                                }
                            }
                        }

                        // ==========================================
                        // APLICA O AIMBOT (Puxa a Mira)
                        // ==========================================
                        if (featureHookToggle && bestTarget && cameraTransform) {
                            
                            // Define qual osso puxar baseado na escolha do menu
                            const char* boneToAim = (aimBone == 0) ? "Head" : "Chest";
                            
                            // Pega a posição exata do osso escolhido
                            Vector3 targetPos = GetBonePosition(bestTarget, boneToAim);
                            
                            // Se falhar ao achar o osso, usa a posição base do inimigo
                            if (targetPos.x == 0 && targetPos.y == 0 && targetPos.z == 0) {
                                get_Position_t get_Position = (get_Position_t)(il2cppBase + OFF_GET_POSITION);
                                targetPos = get_Position((void*)bestTarget);
                            }
                            
                            Vector3 aimAngles = CalculateAngle(localHeadPos, targetPos);
                            set_EulerAngles((void*)cameraTransform, aimAngles);
                        }
                    }
                }
            }
        }
        
        usleep(10000); // 10ms
    }
    return NULL;
}

// ==========================================
// JNI CALLS (O Menu do LGL)
// ==========================================
extern "C" {

JNIEXPORT jobjectArray JNICALL
Java_uk_lgl_modmenu_FloatingModMenuService_getFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;
    MakeToast(env, context, OBFUSCATE("Modded by LGL"), Toast::LENGTH_LONG);

    const char *features[] = {
        OBFUSCATE("Category_Aimbot & ESP"),
        OBFUSCATE("Toggle_Aimbot"),                   // Feature 0
        OBFUSCATE("Spinner_Target Bone_Head,Chest"),  // Feature 1 (0 = Head, 1 = Chest)
        OBFUSCATE("Toggle_ESP")                       // Feature 2
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    return (ret);
}

JNIEXPORT void JNICALL
Java_uk_lgl_modmenu_Preferences_Changes(JNIEnv *env, jclass clazz, jobject obj,
                                        jint featNum, jstring featName, jint value,
                                        jboolean boolean, jstring str) {

    switch (featNum) {
        case 0:
            featureHookToggle = boolean; // Ativa/Desativa Aimbot
            break;
        case 1:
            aimBone = value; // 0 = Cabeça, 1 = Peito
            break;
        case 2:
            Health = boolean; // Ativa/Desativa ESP
            break;
    }
}

}

__attribute__((constructor))
void lib_main() {
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
} char *) IL2CPP_MODULE, dlerror());
        return NULL;
    }

    UnityResolve::Init(p_handler);

    // Start hooking
    LOGI(OBFUSCATE("Starting hooks"));
    Hooks::InitHooks();
    return NULL;
}

__attribute__((constructor))
void init() {
    // Create a new thread so it does not block the main thread, means the game would not freeze
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
    RemapTools::RemapLibrary(OBFUSCATE("libLoader.so"));
}

extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);

    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // Failed to obtain JNIEnv
    }

    if (JNILoader::RegisterAll(env) != JNI_OK)
        return JNI_ERR;

    if (RegisterMenu(env) != 0)
        return JNI_ERR;

    return JNI_VERSION_1_6;
}
