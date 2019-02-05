#include <iostream>
#include <cstdint>
#include <cstring>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>

#include "src/pc_emulator/include/pc_variable.h"
#include "src/pc_emulator/include/pc_datatype.h"
#include "src/pc_emulator/include/pc_configuration.h"
#include "src/pc_emulator/include/pc_resource.h"
#include "src/pc_emulator/include/utils.h"
#include "src/pc_emulator/include/task.h"

using namespace std;
using namespace pc_emulator;
using namespace pc_specification;

using MemType  = pc_specification::MemType;
using DataTypeCategory = pc_specification::DataTypeCategory;
using FieldIntfType = pc_specification::FieldInterfaceType;

using std::string;
using std::vector;
using std::unordered_map;
using std::to_string;


PCConfiguration::PCConfiguration(string ConfigurationPath):
    __ConfigurationPath(ConfigurationPath),
    RegisteredDataTypes(this), RegisteredResources(this) {


        int fileDescriptor = open(ConfigurationPath.c_str(),
                                O_RDONLY);

        if( fileDescriptor < 0 ) {
            std::cerr << " Error opening the specification file " 
                        << std::endl;
            exit(-1);
        }

        google::protobuf::io::FileInputStream 
                                        fileInput(fileDescriptor);
        fileInput.SetCloseOnDelete( true );

        if (!google::protobuf::TextFormat::Parse(&fileInput,
                                        &__specification)) {
            std::cerr << std::endl << "Failed to parse spec file!" 
                << std::endl;
            exit(-1);
        }


        
        __ConfigurationName = "Default_Configuration";
        int logLevel = LogLevels::LOG_INFO;
        string logFilePath = "";
        if (__specification.has_config_name())
            __ConfigurationName = __specification.config_name();
        
        
        if (__specification.has_log_level())
            logLevel = __specification.log_level();

        if (__specification.has_log_file_path())
            logFilePath = __specification.log_file_path();

        PCLogger = new Logger(this, logFilePath, logLevel);
        __RAMmemSize = __specification.machine_spec().ram_mem_size_bytes();
        assert(__RAMmemSize > 0);
        __RAMMemory.AllocateStaticMemory(__RAMmemSize);
        __NumResources = __specification.machine_spec().num_cpus();
        assert(__NumResources > 0);

        std::cout << "Read Configuration !" << std::endl;
        RegisterAllElementaryDataTypes();
        RegisterAllComplexDataTypes();
        RegisterAllResources();

};


void PCConfiguration::RegisterAllResources() {
    for (auto& resource_spec : __specification.machine_spec().resource_spec()) {
        PCResource * new_resource = new PCResource(this,
                                        resource_spec.resource_name(),
                                        resource_spec.input_mem_size_bytes(),
                                        resource_spec.output_mem_size_bytes());
        RegisteredResources.RegisterResource(
                        resource_spec.resource_name(), new_resource);

        new_resource->InitializeAllPoUVars();
    }
}

void PCConfiguration::RegisterAllElementaryDataTypes () {
    for (int Category = DataTypeCategory::BOOL ; 
                Category != DataTypeCategory::ARRAY; Category++) {

        PCDataType * newDataType;
        string DataTypeName, InitValue;

        PCLogger->LogMessage(LogLevels::LOG_INFO, "Next Elementary Category "
            + std::to_string(Category));
        switch(Category) {
            case DataTypeCategory::BOOL :   
                            DataTypeName = "BOOL", InitValue = "0";
                            break;
            case DataTypeCategory::BYTE :     
                                DataTypeName = "BYTE", InitValue = "16#0";
                                break;
            case DataTypeCategory::WORD :     
                                DataTypeName = "WORD", InitValue = "16#0";
                                break;
            case DataTypeCategory::DWORD :     
                                DataTypeName = "DWORD", InitValue = "16#0";
                                break;
            case DataTypeCategory::LWORD :    
                                DataTypeName = "LWORD", InitValue = "16#0";
                                break;
            case DataTypeCategory::CHAR :    
                                DataTypeName = "CHAR", InitValue  = "";
                                break;
            case DataTypeCategory::INT :      
                                DataTypeName = "INT", InitValue = "0";
                                break;
            case DataTypeCategory::SINT :     
                                DataTypeName = "SINT", InitValue = "0";
                                break;
            case DataTypeCategory::DINT :     
                                DataTypeName = "DINT", InitValue = "0";
                                break;
            case DataTypeCategory::LINT :     
                                DataTypeName = "LINT", InitValue = "0";
                                break;
            case DataTypeCategory::UINT :     
                                DataTypeName = "UINT", InitValue = "0";
                                break;
            case DataTypeCategory::USINT :     
                                DataTypeName = "USINT", InitValue = "0";
                                break;
            case DataTypeCategory::UDINT :     
                                DataTypeName = "UDINT", InitValue = "0";
                                break;
            case DataTypeCategory::ULINT :     
                                DataTypeName = "ULINT", InitValue = "0";
                                break;
            case DataTypeCategory::REAL :     
                                DataTypeName = "REAL", InitValue = "0.0";
                                break;
            case DataTypeCategory::LREAL :     
                                DataTypeName = "LREAL", InitValue = "0.0";
                                break;
            case DataTypeCategory::TIME :     
                                DataTypeName = "TIME", InitValue = "t#0s";
                                break;
            case DataTypeCategory::DATE :     
                                DataTypeName = "DATE", InitValue = "d#0001-01-01";
                                break;
            case DataTypeCategory::TIME_OF_DAY :     
                                DataTypeName = "TOD", InitValue = "tod#00:00:00";
                                break;
            case DataTypeCategory::DATE_AND_TIME :     
                                DataTypeName = "DT";
                                InitValue = "dt#0001-01-01-00:00:00";
                                break;
            default:            DataTypeName = "NA";
                                break;
     
        }

        __DataTypeDefaultInitialValues.insert(std::make_pair(
                                                Category, InitValue));

        if(DataTypeName != "NA") {
            PCLogger->LogMessage(LogLevels::LOG_INFO, 
                    "Registering Elementary DataType: " + DataTypeName);
            newDataType = new PCDataType(this, DataTypeName, DataTypeName,
                                        static_cast<DataTypeCategory>(Category),
                                        InitValue);   
            RegisteredDataTypes.RegisterDataType(DataTypeName, newDataType);  
        } 
    }

    PCLogger->LogMessage(LogLevels::LOG_INFO, "Registering STRING DataType");
    // note that max string length is 1000 chars
    PCDataType * stringDataType = new PCDataType(this, "STRING", "CHAR",
                                1000, DataTypeCategory::ARRAY);
    RegisteredDataTypes.RegisterDataType("STRING", stringDataType);

    __DataTypeDefaultInitialValues.insert(std::make_pair(
                                            DataTypeCategory::ARRAY, ""));
    __DataTypeDefaultInitialValues.insert(std::make_pair(
                                            DataTypeCategory::DERIVED, ""));
    __DataTypeDefaultInitialValues.insert(std::make_pair(
                                            DataTypeCategory::POU, ""));

    PCLogger->LogMessage(LogLevels::LOG_INFO, 
                    "Registered all elementary datatypes!\n");
}

void PCConfiguration::RegisterAllComplexDataTypes() {
    if (!__specification.has_config_global_pou_var())
        __global_pou_var = nullptr;
    else {

        PCDataType * global_var_type = new PCDataType(this, 
                    "__CONFIG_GLOBAL__", "__CONFIG_GLOBAL__",
                    DataTypeCategory::POU);

        Utils::InitializeDataType(this, global_var_type,
             __specification.config_global_pou_var());
        
        RegisteredDataTypes.RegisterDataType("__CONFIG_GLOBAL__", 
                                        global_var_type);

        __global_pou_var = new PCVariable(this, nullptr,
                                "__CONFIG_GLOBAL_VAR__", "__CONFIG_GLOBAL__");
        __global_pou_var->AllocateAndInitialize();
        
    }
    
    if (!__specification.has_config_access_pou_var())
        __access_pou_var = nullptr;
    else {
        PCDataType * access_var_type = new PCDataType(this, 
                    "__CONFIG_ACCESS__", "__CONFIG_ACCESS__",
                    DataTypeCategory::POU);

        RegisteredDataTypes.RegisterDataType("__CONFIG__ACCESS__", 
                                        access_var_type);

        Utils::InitializeAccessDataType(this, access_var_type,
             __specification.config_access_pou_var());
        __access_pou_var = new PCVariable(this, nullptr,
                                "__CONFIG_ACCESS_VAR__", "__CONFIG_ACCESS__");
        __access_pou_var->AllocateAndInitialize();

        // now we need to set pointers to some fields of this variable

        for (auto& field : 
                    __specification.config_access_pou_var().datatype_field()) {
            if (field.intf_type() == FieldIntfType::VAR_ACCESS 
                    && field.has_field_storage_spec()) {

            
                if (field.field_storage_spec().has_full_storage_spec()) {
                    //extract memtype, byte and bit offsets from string specification
                    int mem_type, ByteOffset, BitOffset;
                    if (!Utils::ExtractFromAccessStorageSpec(
                            this,
                            field.field_storage_spec().full_storage_spec(),
                            &mem_type, &ByteOffset, &BitOffset)) {
                        // these are the fields which are selected
                        string StorageSpec 
                            = field.field_storage_spec().full_storage_spec();

                        PCVariable * desired_ptr = GetVariable(StorageSpec);
                        if(!desired_ptr) {
                            PCLogger->RaiseException("Error in storage spec"
                                    " of access variable!");
                        }

                        //set this as a ptr to the field of acess variable
                        __access_pou_var->SetPtr(field.field_name(),
                                        desired_ptr);

                    }
                }
            }
        }

    }

    for (auto & datatype_decl : __specification.datatype_declaration()) {
        if (datatype_decl.datatype_category() != DataTypeCategory::POU
        && datatype_decl.datatype_category() != DataTypeCategory::ARRAY
        && datatype_decl.datatype_category() != DataTypeCategory::NOT_ASSIGNED) {
            if (datatype_decl.datatype_field_size() > 0
                && datatype_decl.has_datatype_spec()) {
                    PCLogger->RaiseException("Incorrect proto specification "
                     "cannot have array_spec and datatype_fields specified "
                     "at the same time!");
            }

            PCDataType * new_data_type = nullptr;
            PCLogger->LogMessage(LogLevels::LOG_INFO, 
                "Registering New DataType: " + datatype_decl.name());

            if (datatype_decl.has_datatype_spec()) {
                assert (datatype_decl.datatype_category() 
                    != DataTypeCategory::DERIVED);

                string elementary_datatype_name 
                    = Utils::GetElementaryDataTypeName(
                        datatype_decl.datatype_category());
                string InitValue 
                    = datatype_decl.datatype_spec().has_initial_value() ? 
                      datatype_decl.datatype_spec().initial_value(): "" ;
                s64 range_min =  datatype_decl.datatype_spec().has_range_min() ? 
                      datatype_decl.datatype_spec().range_min() : LLONG_MIN;
                s64 range_max = datatype_decl.datatype_spec().has_range_max() ? 
                      datatype_decl.datatype_spec().range_max(): LLONG_MAX;
                

                if (datatype_decl.datatype_spec().has_dimension_1()) {
                    if (!datatype_decl.datatype_spec().has_dimension_2()) {
                        assert(datatype_decl.datatype_spec().dimension_1() > 0);
                        
                        new_data_type = new PCDataType(this, 
                        datatype_decl.name(), elementary_datatype_name,
                        datatype_decl.datatype_spec().dimension_1(),
                        datatype_decl.datatype_category(), InitValue,
                        range_min, range_max);

                    } else {// 2-d array datatype
                        assert(datatype_decl.datatype_spec().dimension_1() > 0
                            && datatype_decl.datatype_spec().dimension_2() > 0);
                        new_data_type = new PCDataType(this, 
                        datatype_decl.name(), elementary_datatype_name,
                        datatype_decl.datatype_spec().dimension_1(),
                        datatype_decl.datatype_spec().dimension_2(),
                        datatype_decl.datatype_category(), InitValue,
                        range_min, range_max);
                    }
                } else {
                    new_data_type = new PCDataType(this, 
                        datatype_decl.name(), elementary_datatype_name,
                        datatype_decl.datatype_category(),
                        InitValue, range_min, range_max);
                }

                


            } else {
                assert(datatype_decl.datatype_category() 
                    == DataTypeCategory::DERIVED 
                    && datatype_decl.datatype_field_size() > 0);
                new_data_type = new PCDataType(this,
                    datatype_decl.name(), datatype_decl.name(),
                    DataTypeCategory::DERIVED);
                Utils::InitializeDataType(this, new_data_type, datatype_decl);
            }

            
            assert(new_data_type != nullptr);
            RegisteredDataTypes.RegisterDataType(datatype_decl.name(), 
                                        new_data_type);
        }

    }

    PCLogger->LogMessage(LogLevels::LOG_INFO, "Registered all Complex DataTypes!");
}

PCVariable * PCConfiguration::GetVariablePointerToMem(int memType,
                int ByteOffset, int BitOffset,string VariableDataTypeName) {

    assert(ByteOffset > 0 && BitOffset >= 0 && BitOffset < 8);
    assert(memType == MemType::RAM_MEM);
    string VariableName = __ConfigurationName 
                            + "." + VariableDataTypeName
                            + "." + std::to_string(memType)
                            + "." + std::to_string(ByteOffset)
                            + "." + std::to_string(BitOffset);
    // need to track and delete this variable later on
    auto got = __AccessedFields.find(VariableName);
    if(got == __AccessedFields.end()) {
        PCVariable* V = new PCVariable(this, nullptr, VariableName,
                                    VariableDataTypeName);
        assert(V != nullptr);

        
        V->__MemoryLocation.SetMemUnitLocation(&__RAMMemory);
        V->__ByteOffset = ByteOffset;
        V->__BitOffset = BitOffset;
        V->__IsDirectlyRepresented = true;
        V->__MemAllocated = true;
        V->AllocateAndInitialize();
        __AccessedFields.insert(std::make_pair(VariableName, V));

        return V;
    } else {
        return got->second;
    }
}

PCVariable * PCConfiguration::GetVariablePointerToResourceMem(
                        string ResourceName,
                        int MemType, int ByteOffset,
                        int BitOffset, string VariableDataTypeName){
    PCResource * desired_resource = RegisteredResources.GetResource(
                                        ResourceName);
    if (!desired_resource)
        return nullptr;
    
    return desired_resource->GetVariablePointerToMem(MemType, ByteOffset,
                                    BitOffset, VariableDataTypeName);

}

PCVariable * PCConfiguration::GetVariable(string NestedFieldName) {
    assert(!NestedFieldName.empty());
    std::vector<string> results;
    boost::split(results, NestedFieldName, [](char c){return c == '.';});

    if  (results.size() == 1) {
        //no dot was found, try the global_variable
        if (__global_pou_var 
            && __global_pou_var->__VariableDataType->IsFieldPresent(
                                                    NestedFieldName))
            return __global_pou_var->GetPCVariableToField(NestedFieldName);
        return nullptr;
    } else {
        // dot was found; could be of the form resource.field_name
        PCResource * resource = RegisteredResources.GetResource(results[0]);
        if (resource == nullptr) {
            if (__global_pou_var 
            && __global_pou_var->__VariableDataType->IsFieldPresent(
                                                    NestedFieldName))
                return __global_pou_var->GetPCVariableToField(NestedFieldName);
            else
                return nullptr;
            
        } else {
            
            string ResourceVariableName = NestedFieldName.substr(
                    NestedFieldName.find('.') + 1, string::npos);
            return resource->GetVariable(ResourceVariableName);
        }
    } 
}

PCVariable * PCConfiguration::GetAccessVariable(string NestedFieldName) {

    if (!__access_pou_var || NestedFieldName.empty())
        return nullptr;
    
    if (__access_pou_var->__VariableDataType->IsFieldPresent(NestedFieldName))
        return __access_pou_var->GetPCVariableToField(NestedFieldName);
    else
        return nullptr;
    
}

PCDataType * PCConfiguration::LookupDataType(string DataTypeName) {
    return RegisteredDataTypes.GetDataType(DataTypeName);
}

void PCConfiguration::Cleanup() {
    for ( auto it = __AccessedFields.begin(); it != __AccessedFields.end(); 
            ++it ) {
            PCVariable * __AccessedVariable = it->second;
            __AccessedVariable->Cleanup();
            delete __AccessedVariable;
    }

    if (__global_pou_var) {
        __global_pou_var->Cleanup();
        delete __global_pou_var;
    }

    if (__access_pou_var) {
        __access_pou_var->Cleanup();
        delete __access_pou_var;
    }

    RegisteredResources.Cleanup();
    RegisteredDataTypes.Cleanup();
}
