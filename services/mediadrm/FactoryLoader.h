/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MEDIA_CAS_LOADER_H_
#define MEDIA_CAS_LOADER_H_

#include <dirent.h>
#include <dlfcn.h>
#include <media/SharedLibrary.h>
#include <utils/KeyedVector.h>
#include <utils/Mutex.h>

namespace android {
using namespace std;
using namespace media;
using namespace MediaCas;

template <class T>
class FactoryLoader {
public:
    FactoryLoader(const char *name) :
        mFactory(NULL), mCreateFactoryFuncName(name) {}

    virtual ~FactoryLoader() { closeFactory(); }

    bool findFactoryForScheme(
            int32_t CA_system_id,
            sp<SharedLibrary> *library = NULL,
            T** factory = NULL);

    bool enumeratePlugins(vector<ParcelableCasPluginDescriptor>* results);

private:
    typedef T*(*CreateFactoryFunc)();

    Mutex mMapLock;
    T* mFactory;
    const char *mCreateFactoryFuncName;
    sp<SharedLibrary> mLibrary;
    KeyedVector<int32_t, String8> mCASystemIdToLibraryPathMap;
    KeyedVector<String8, wp<SharedLibrary> > mLibraryPathToOpenLibraryMap;

    bool loadFactoryForSchemeFromPath(
            const String8 &path,
            int32_t CA_system_id,
            sp<SharedLibrary> *library,
            T** factory);

    bool queryPluginsFromPath(
            const String8 &path,
            vector<ParcelableCasPluginDescriptor>* results);

    bool openFactory(const String8 &path);
    void closeFactory();
};

template <class T>
bool FactoryLoader<T>::findFactoryForScheme(
        int32_t CA_system_id, sp<SharedLibrary> *library, T** factory) {
    if (library != NULL) {
        library->clear();
    }
    if (factory != NULL) {
        *factory = NULL;
    }

    Mutex::Autolock autoLock(mMapLock);

    // first check cache
    ssize_t index = mCASystemIdToLibraryPathMap.indexOfKey(CA_system_id);
    if (index >= 0) {
        return loadFactoryForSchemeFromPath(
                mCASystemIdToLibraryPathMap[index],
                CA_system_id, library, factory);
    }

    // no luck, have to search
    String8 dirPath("/system/lib/mediacas");
    DIR* pDir = opendir(dirPath.string());

    if (pDir == NULL) {
        ALOGE("Failed to open plugin directory %s", dirPath.string());
        return false;
    }

    struct dirent* pEntry;
    while ((pEntry = readdir(pDir))) {
        String8 pluginPath = dirPath + "/" + pEntry->d_name;
        if (pluginPath.getPathExtension() == ".so") {
            if (loadFactoryForSchemeFromPath(
                    pluginPath, CA_system_id, library, factory)) {
                mCASystemIdToLibraryPathMap.add(CA_system_id, pluginPath);
                closedir(pDir);

                return true;
            }
        }
    }

    closedir(pDir);

    ALOGE("Failed to find plugin");
    return false;
}

template <class T>
bool FactoryLoader<T>::enumeratePlugins(
        vector<ParcelableCasPluginDescriptor>* results) {
    ALOGI("enumeratePlugins");

    results->clear();

    String8 dirPath("/system/lib/mediacas");
    DIR* pDir = opendir(dirPath.string());

    if (pDir == NULL) {
        ALOGE("Failed to open plugin directory %s", dirPath.string());
        return false;
    }

    Mutex::Autolock autoLock(mMapLock);

    struct dirent* pEntry;
    while ((pEntry = readdir(pDir))) {
        String8 pluginPath = dirPath + "/" + pEntry->d_name;
        if (pluginPath.getPathExtension() == ".so") {
            queryPluginsFromPath(pluginPath, results);
        }
    }
    return true;
}

template <class T>
bool FactoryLoader<T>::loadFactoryForSchemeFromPath(
        const String8 &path, int32_t CA_system_id,
        sp<SharedLibrary> *library, T** factory) {
    closeFactory();

    if (!openFactory(path) || !mFactory->isSystemIdSupported(CA_system_id)) {
        closeFactory();
        return false;
    }

    if (library != NULL) {
        *library = mLibrary;
    }
    if (factory != NULL) {
        *factory = mFactory;
    }
    return true;
}

template <class T>
bool FactoryLoader<T>::queryPluginsFromPath(
        const String8 &path, vector<ParcelableCasPluginDescriptor>* results) {
    closeFactory();

    vector<CasPluginDescriptor> descriptors;
    if (!openFactory(path) || mFactory->queryPlugins(&descriptors) != OK) {
        closeFactory();
        return false;
    }

    for (auto it = descriptors.begin(); it != descriptors.end(); it++) {
        results->push_back(ParcelableCasPluginDescriptor(
                it->CA_system_id, it->name));
    }
    return true;
}

template <class T>
bool FactoryLoader<T>::openFactory(const String8 &path) {
    // get strong pointer to open shared library
    ssize_t index = mLibraryPathToOpenLibraryMap.indexOfKey(path);
    if (index >= 0) {
        mLibrary = mLibraryPathToOpenLibraryMap[index].promote();
    } else {
        index = mLibraryPathToOpenLibraryMap.add(path, NULL);
    }

    if (!mLibrary.get()) {
        mLibrary = new SharedLibrary(path);
        if (!*mLibrary) {
            return false;
        }

        mLibraryPathToOpenLibraryMap.replaceValueAt(index, mLibrary);
    }

    CreateFactoryFunc createFactory =
        (CreateFactoryFunc)mLibrary->lookup(mCreateFactoryFuncName);
    if (createFactory == NULL || (mFactory = createFactory()) == NULL) {
        return false;
    }
    return true;
}

template <class T>
void FactoryLoader<T>::closeFactory() {
    delete mFactory;
    mFactory = NULL;
    mLibrary.clear();
}

} // namespace android

#endif // MEDIA_CAS_LOADER_H_
