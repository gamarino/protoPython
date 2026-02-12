#ifndef PROTOPYTHON_COMPILEDMODULEPROVIDER_H
#define PROTOPYTHON_COMPILEDMODULEPROVIDER_H

#include <protoCore.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace protoPython {

class CompiledModuleProvider : public proto::ModuleProvider {
public:
    CompiledModuleProvider(std::vector<std::string> basePaths);
    virtual ~CompiledModuleProvider();

    virtual const proto::ProtoObject* tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) override;
    virtual const std::string& getGUID() const override { return guid_; }
    virtual const std::string& getAlias() const override { return alias_; }

private:
    std::vector<std::string> basePaths_;
    std::string guid_;
    std::string alias_;
    std::unordered_map<std::string, void*> loadedHandles_;
};

} // namespace protoPython

#endif // PROTOPYTHON_COMPILEDMODULEPROVIDER_H
