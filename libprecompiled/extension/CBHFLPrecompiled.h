#pragma once
#include <libprecompiled/Common.h>
#include <libnlohmann_json/single_include/nlohmann/json.hpp>
#define OUTPUT 1

using json = nlohmann::json;

namespace dev
{
namespace precompiled
{

class CBHFLPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<CBHFLPrecompiled> Ptr;  // 指针
    CBHFLPrecompiled();                             // 构造
    virtual ~CBHFLPrecompiled(){};                  // 析构

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;  // 重载call函数

private:
    void InitGlobalModel(
        storage::Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult);
    void InsertVariable(storage::Table::Ptr table, Address const& _origin,
        PrecompiledExecResult::Ptr callResult, const std::string& Key, std::string& strValue);
    std::string GetVariable(storage::Table::Ptr table, Address const& _origin,
        PrecompiledExecResult::Ptr callResult, const std::string& Key);
    void UpdateVariable(storage::Table::Ptr table, Address const& _origin,
        PrecompiledExecResult::Ptr callResult, const std::string& Key, std::string& strValue);
};
}  // namespace precompiled
}  // namespace dev