#ifndef PROTOPYTHON_COMPILEDMODULEPROVIDER_H
#define PROTOPYTHON_COMPILEDMODULEPROVIDER_H

#include <protoCore.h>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace protoPython {

class CompiledModuleProvider : public proto::ModuleProvider {
public:
    /** `suffix` tells a later environment's provider apart (PythonEnvironment::providerSuffix). */
    CompiledModuleProvider(std::vector<std::string> basePaths, const std::string& suffix = "");
    virtual ~CompiledModuleProvider();

    virtual const proto::ProtoObject* tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) override;
    virtual const std::string& getGUID() const override { return guid_; }
    virtual const std::string& getAlias() const override { return alias_; }

private:
    std::vector<std::string> basePaths_;
    std::string guid_;
    std::string alias_;
    // Guarded by mutex_: module resolution is not serialised by a lock.
    std::mutex mutex_;
    std::unordered_map<std::string, void*> loadedHandles_;
};

} // namespace protoPython

#endif // PROTOPYTHON_COMPILEDMODULEPROVIDER_H
