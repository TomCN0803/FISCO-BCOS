#include "CBHFLPrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <typeinfo>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// data struct
typedef std::unordered_map<std::string, std::string> Pair;
typedef std::unordered_map<std::string, float> Scores;


// role names
const std::string ROLE_TRAINER = "trainer";
const std::string ROLE_AGG = "agg";

const std::string TABLE_NAME = "CBHFLPrecompiled";  // table name

const std::string KEY_FIELD = "key";                    // key field
const std::string VALUE_FIELD = "value";                // value field

const std::string EPOCH_FIELD_NAME = "epoch";  // 当前迭代轮次(int)-->触发链下训练
const std::string ROLES_FIELD_NAME = "roles";  // 保存各个客户端的角色(unordered_map<Adress,string>)
const std::string GLOBAL_PROTOS_FIELD_NAME = "global_protos";  // 保存全局Protos(string)
const std::string LOCAL_PROTOS_UPDATES_FIELD_NAME =
    "local_protos_updates";  // 保存所有已上传的Protos更新(unordered_map<Adress,string>)

// interface name
const char* const REGISTER_NODE = "RegisterNode(string)";                    // 节点注册
const char* const QUERY_CURRENT_EPOCH = "QueryCurrentEpoch()";               // 获取当前epoch
const char* const QUERY_GLOBAL_PROTOS = "QueryGlobalProtos()";               // 获取全局Protos
const char* const UPLOAD_LOCAL_PROTOS = "UploadLocalProtos(string,int256)";  // 上传本地Protos
const char* const QUERY_PROTOS_UPDATES = "QueryProtosUpdates()";  // 获取所有本地Protos更新
const char* const UPDATE_GLOBAL_PROTOS = "UpdateGlobalProtos(string,int256)";  // 更新全局Protos

template <class T>
inline std::string to_json_string(T& t)
{
    json j = t;
    return j.dump();
}

CommitteePrecompiled::CommitteePrecompiled()  // 构造，初始化函数选择器对应的函数
{
    name2Selector[REGISTER_NODE] = getFuncSelector(REGISTER_NODE);
    name2Selector[QUERY_CURRENT_EPOCH] = getFuncSelector(QUERY_CURRENT_EPOCH);
    name2Selector[QUERY_GLOBAL_PROTOS] = getFuncSelector(QUERY_GLOBAL_PROTOS);
    name2Selector[UPLOAD_LOCAL_PROTOS] = getFuncSelector(UPLOAD_LOCAL_PROTOS);
    name2Selector[QUERY_PROTOS_UPDATES] = getFuncSelector(QUERY_PROTOS_UPDATES);
    name2Selector[UPDATE_GLOBAL_PROTOS] = getFuncSelector(UPDATE_GLOBAL_PROTOS);
}

PrecompiledExecResult::Ptr CommitteePrecompiled::call(
    dev::blockverifier::ExecutiveContext::Ptr _context, bytesConstRef _param,
    Address const& _origin, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);       // 六位函数选择器
    bytesConstRef data = getParamData(_param);  // 参数
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();  // 声明结果
    callResult->gasPricer()->setMemUsed(_param.size());                           // 分配内存
    dev::eth::ContractABI abi;                                                    // 声明abi

    // the hash as a user-readable hex string with 0x perfix
    std::string _origin_str = _origin.hexPrefixed();

    // open table if table is exist
    Table::Ptr table = openTable(_context, precompiled::getTableName(TABLE_NAME));
    callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);
    // table is not exist, create it.
    if (!table)
    {
        table = createTable(
            _context, precompiled::getTableName(TABLE_NAME), KEY_FIELD, VALUE_FIELD, _origin);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::CreateTable);
        if (!table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                                   << LOG_DESC("open table failed.");
            getErrorCodeOut(callResult->mutableExecResult(), storage::CODE_NO_AUTHORIZED);
            return callResult;
        }
        // init global model
        InitGlobalModel(table, _origin, callResult);
    }
    if (func == name2Selector[REGISTER_NODE])
    {
        std::string role;
        abi.abiOut(data, role);
#if OUTPUT
        std::clog << "register for " << _origin_str << std::endl << "role: " << role << std::endl;
#endif

        std::string roles_str = GetVariable(table, _origin, callResult, ROLES_FIELD_NAME);
        Pair roles = json::parse(roles_str);

        if (roles.find(_origin_str) == roles.end())
        {
            if (role == ROLE_AGG)
            {
                // set epoch to 0 and begin training.
                int epoch = 0;
                std::string epoch_str = to_json_string(epoch);
                UpdateVariable(table, _origin, callResult, EPOCH_FIELD_NAME, epoch_str);
            }
            roles[_origin_str] = role != ROLE_AGG ? ROLE_TRAINER : ROLE_AGG;

            roles_str = to_json_string(roles);
            UpdateVariable(table, _origin, callResult, ROLES_FIELD_NAME, roles_str);
        }
        else
        {
#if OUTPUT
            std::clog << "node already registered" << std::endl;
#endif
        }
    }
    else if (func == name2Selector[QUERY_CURRENT_EPOCH])
    {
        // 获取当前epoch
        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);
        callResult->setExecResult(abi.abiIn("", s256(epoch)));
    }
    else if (func == name2Selector[QUERY_GLOBAL_PROTOS])
    {
        // 获取全局Protos
        std::string global_protos_str =
            GetVariable(table, _origin, callResult, GLOBAL_PROTOS_FIELD_NAME);
        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);
        callResult->setExecResult(abi.abiIn("", global_protos_str, s256(epoch)));
    }
    else if (func == name2Selector[UPLOAD_LOCAL_PROTOS])
    {
        // 上传本地Protos
        std::string protos;
        s256 ep;
        abi.abiOut(data, protos, ep);

        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);

        // not current epoch
        if (ep != epoch)
            return callResult;

        // add local protos updates to table
        std::string local_protos_updates_str =
            GetVariable(table, _origin, callResult, LOCAL_PROTOS_UPDATES_FIELD_NAME);
        Pair local_protos_updates = json::parse(local_protos_updates_str);
        local_protos_updates[_origin_str] = protos;
        local_protos_updates_str = to_json_string(local_protos_updates);
        UpdateVariable(
            table, _origin, callResult, LOCAL_PROTOS_UPDATES_FIELD_NAME, local_protos_updates_str);

#if OUTPUT
        std::clog << "the protos of local model is collected" << std::endl;
#endif
    }
    else if (func == name2Selector[QUERY_PROTOS_UPDATES])
    {
        // 获取所有本地Protos更新
        std::string local_protos_updates_str =
            GetVariable(table, _origin, callResult, LOCAL_PROTOS_UPDATES_FIELD_NAME);
        callResult->setExecResult(abi.abiIn("", local_protos_updates_str));
    }
    else if (func == name2Selector[UPDATE_GLOBAL_PROTOS])
    {
        // 更新全局Protos
        std::string protos;
        s256 ep;
        abi.abiOut(data, protos, ep);

        std::string epoch_str = GetVariable(table, _origin, callResult, EPOCH_FIELD_NAME);
        int epoch = json::parse(epoch_str);

        // not current epoch
        if (ep != epoch)
            return callResult;

        UpdateVariable(table, _origin, callResult, GLOBAL_PROTOS_FIELD_NAME, protos);

        // update global epoch
        epoch += 1;
        epoch_str = to_json_string(epoch);
        UpdateVariable(table, _origin, callResult, EPOCH_FIELD_NAME, epoch_str);
    }
    else
    {  // unknown function call
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC(" unknown func ")
                               << LOG_KV("func", func);
        callResult->setExecResult(abi.abiIn("", u256(CODE_UNKNOW_FUNCTION_CALL)));
    }

    return callResult;
}

// init global model
void CommitteePrecompiled::InitGlobalModel(
    Table::Ptr table, Address const& _origin, PrecompiledExecResult::Ptr callResult)
{
    int epoch = -999;
    std::string epoch_str = to_json_string(epoch);
    InsertVariable(table, _origin, callResult, EPOCH_FIELD_NAME, epoch_str);
    Pair roles;
    std::string roles_str = to_json_string(roles);
    InsertVariable(table, _origin, callResult, ROLES_FIELD_NAME, roles_str);
    Pair local_protos_updates;
    std::string local_protos_updates_str = to_json_string(local_protos_updates);
    InsertVariable(
        table, _origin, callResult, LOCAL_PROTOS_UPDATES_FIELD_NAME, local_protos_updates_str);
    std::string global_protos_str = "";
    InsertVariable(table, _origin, callResult, GLOBAL_PROTOS_FIELD_NAME, global_protos_str);
}


// insert variable
void CommitteePrecompiled::InsertVariable(Table::Ptr table, Address const& _origin,
    PrecompiledExecResult::Ptr callResult, const std::string& Key, std::string& strValue)
{
    int count = 0;
    auto entry = table->newEntry();
    entry->setField(KEY_FIELD, Key);
    entry->setField(VALUE_FIELD, strValue);
    count = table->insert(Key, entry, std::make_shared<AccessOptions>(_origin));
    if (count > 0)
    {
        callResult->gasPricer()->updateMemUsed(entry->capacity() * count);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Insert, count);
    }
    if (count == storage::CODE_NO_AUTHORIZED)
    {  //  permission denied
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC("set")
                               << LOG_DESC("permission denied");
    }
    getErrorCodeOut(callResult->mutableExecResult(), count);
}

// get variable
std::string CommitteePrecompiled::GetVariable(
    Table::Ptr table, Address const&, PrecompiledExecResult::Ptr callResult, const std::string& Key)
{
    auto entries = table->select(Key, table->newCondition());
    std::string retValue = "";
    if (0u != entries->size())
    {
        callResult->gasPricer()->updateMemUsed(getEntriesCapacity(entries));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());
        auto entry = entries->get(0);
        retValue = entry->getField(VALUE_FIELD);
    }
    return retValue;
}

// update variable
void CommitteePrecompiled::UpdateVariable(Table::Ptr table, Address const& _origin,
    PrecompiledExecResult::Ptr callResult, const std::string& Key, std::string& strValue)
{
    int count = 0;
    auto entry = table->newEntry();
    entry->setField(KEY_FIELD, Key);
    entry->setField(VALUE_FIELD, strValue);
    count =
        table->update(Key, entry, table->newCondition(), std::make_shared<AccessOptions>(_origin));
    if (count > 0)
    {
        callResult->gasPricer()->updateMemUsed(entry->capacity() * count);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Update, count);
    }
    if (count == storage::CODE_NO_AUTHORIZED)
    {  //  permission denied
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CommitteePrecompiled") << LOG_DESC("set")
                               << LOG_DESC("permission denied");
    }
    getErrorCodeOut(callResult->mutableExecResult(), count);
}
