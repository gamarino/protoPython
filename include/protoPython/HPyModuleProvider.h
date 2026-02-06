#ifndef PROTOPYTHON_HPYMODULEPROVIDER_H
#define PROTOPYTHON_HPYMODULEPROVIDER_H

#include <protoCore.h>
#include <protoPython/HPyContext.h>
#include <string>
#include <vector>

namespace protoPython {

/**
 * HPyModuleProvider: Resolves and loads HPy extension modules (.so / .hpy.so).
 * Steps 1205-1207.
 */
class HPyModuleProvider : public proto::ModuleProvider {
public:
    explicit HPyModuleProvider(std::vector<std::string> basePaths);
    virtual ~HPyModuleProvider();

    virtual const proto::ProtoObject* tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) override;
    virtual const std::string& getGUID() const override { return guid_; }
    virtual const std::string& getAlias() const override { return alias_; }

private:
    std::vector<std::string> basePaths_;
    std::string guid_;
    std::string alias_;

    // Prevent multiple loads of same module in same provider
    std::map<std::string, void*> loadedHandles_;
};

} // namespace protoPython

#endif // PROTOPYTHON_HPYMODULEPROVIDER_H
