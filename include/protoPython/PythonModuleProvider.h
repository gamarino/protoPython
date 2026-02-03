#ifndef PROTOPYTHON_PYTHONMODULEPROVIDER_H
#define PROTOPYTHON_PYTHONMODULEPROVIDER_H

#include <protoCore.h>
#include <string>

namespace protoPython {

class PythonModuleProvider : public proto::ModuleProvider {
public:
    PythonModuleProvider(std::vector<std::string> basePaths);
    virtual ~PythonModuleProvider() = default;

    virtual const proto::ProtoObject* tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) override;
    virtual const std::string& getGUID() const override { return guid_; }
    virtual const std::string& getAlias() const override { return alias_; }

private:
    std::vector<std::string> basePaths_;
    std::string guid_;
    std::string alias_;
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONMODULEPROVIDER_H
