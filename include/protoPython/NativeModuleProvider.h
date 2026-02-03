#ifndef PROTOPYTHON_NATIVEMODULEPROVIDER_H
#define PROTOPYTHON_NATIVEMODULEPROVIDER_H

#include <protoCore.h>
#include <string>
#include <map>
#include <functional>

namespace protoPython {

using ModuleInitializer = std::function<const proto::ProtoObject*(proto::ProtoContext*)>;

class NativeModuleProvider : public proto::ModuleProvider {
public:
    NativeModuleProvider();
    virtual ~NativeModuleProvider() = default;

    void registerModule(const std::string& name, ModuleInitializer init);

    virtual const proto::ProtoObject* tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) override;
    virtual const std::string& getGUID() const override { return guid_; }
    virtual const std::string& getAlias() const override { return alias_; }

private:
    std::string guid_;
    std::string alias_;
    std::map<std::string, ModuleInitializer> modules_;
};

} // namespace protoPython

#endif // PROTOPYTHON_NATIVEMODULEPROVIDER_H
