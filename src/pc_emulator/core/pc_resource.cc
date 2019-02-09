#include <assert.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <regex>
#include <vector>
#include <queue>

#include "src/pc_emulator/include/pc_pou_code_container.h"
#include "src/pc_emulator/include/pc_datatype.h"
#include "src/pc_emulator/include/pc_variable.h"
#include "src/pc_emulator/include/pc_resource.h"
#include "src/pc_emulator/include/pc_configuration.h"
#include "src/pc_emulator/include/utils.h"
#include "src/pc_emulator/include/task.h"

using namespace std;
using namespace pc_emulator;
using namespace pc_specification;
using MemType  = pc_specification::MemType;
using DataTypeCategory = pc_specification::DataTypeCategory;
using FieldIntfType = pc_specification::FieldInterfaceType;


void PCResource::RegisterPoUVariable(string VariableName, PCVariable * Var) {
    std::unordered_map<std::string, PCVariable*>::const_iterator got = 
                        __ResourcePoUVars.find (VariableName);
    if (got != __ResourcePoUVars.end()) {
        
        __configuration->PCLogger->RaiseException("Variable already defined !");
    } else {
        __ResourcePoUVars.insert(std::make_pair(VariableName, Var));
       
        __configuration->PCLogger->LogMessage(LogLevels::LOG_INFO,
                                        "Registered new resource pou variable!");
    }

}

PCVariable * PCResource::GetVariable(string NestedFieldName) {
    assert(!NestedFieldName.empty());
    DataTypeFieldAttributes FieldAttributes;
    auto got = __ResourcePoUVars.find(
            "__RESOURCE_" + __ResourceName + "_GLOBAL__");
    PCVariable * global_var;

    if (got == __ResourcePoUVars.end())
        global_var = nullptr;
    else {
        global_var = got->second;
    }
    
    if (global_var != nullptr && 
        global_var->__VariableDataType->IsFieldPresent(NestedFieldName)){

        global_var->GetFieldAttributes(NestedFieldName, FieldAttributes);

        if (!Utils::IsFieldTypePtr(FieldAttributes.FieldInterfaceType))
            return global_var->GetPCVariableToField(NestedFieldName);
        else
            return global_var->GetPtrStoredAtField(NestedFieldName);
    }
    else {
        // this may be referring to a PoU variable
        auto got = __ResourcePoUVars.find(NestedFieldName);
            if (got == __ResourcePoUVars.end()) {
                return nullptr;
        } else {
            return got->second;
        }
    }
}

PCVariable * PCResource::GetPoUVariable(string PoUName) {
    auto got =  __ResourcePoUVars.find(PoUName);
    if (got == __ResourcePoUVars.end()) {
        return nullptr;
    }
    return got->second;
}

// Returns a global variable or a directly represented variable declared
// by any of the POUs defined in this resource provided the NestedFieldName matches
PCVariable * PCResource::GetPOUGlobalVariable(string NestedFieldName) {
    assert(!NestedFieldName.empty());
    std::vector<string> results;
    boost::split(results, NestedFieldName,
                boost::is_any_of("."), boost::token_compress_on);

    for (auto it = __ResourcePoUVars.begin(); 
                    it != __ResourcePoUVars.end(); it ++) {
        PCVariable * var = it->second;
        if (var->__VariableDataType->IsFieldPresent(NestedFieldName)) {
            DataTypeFieldAttributes FieldAttributes;
            var->GetFieldAttributes(NestedFieldName, 
                                    FieldAttributes);
            if (FieldAttributes.FieldInterfaceType 
                == FieldIntfType::VAR_GLOBAL)
                return var->GetPCVariableToField(NestedFieldName);

            if (FieldAttributes.FieldInterfaceType 
                == FieldIntfType::VAR_EXPLICIT_STORAGE) {
                return var->GetPtrStoredAtField(NestedFieldName);
            }
        }
    }

    return nullptr;
}

void PCResource::InitializeAllPoUVars() {

    
    for (auto & resource_spec : 
            __configuration->__specification.machine_spec().resource_spec()) {
        if (resource_spec.resource_name() == __ResourceName) {
            if (resource_spec.has_resource_global_var()) {
                PCDataType * global_var_type = new PCDataType(
                    __configuration, 
                    "__RESOURCE_" + __ResourceName + "_GLOBAL__",
                    "__RESOURCE_" + __ResourceName + "_GLOBAL__",
                    DataTypeCategory::POU);

                __configuration->RegisteredDataTypes.RegisterDataType(
                "__RESOURCE_" + __ResourceName + "_GLOBAL__", global_var_type);

                Utils::InitializeDataType(__configuration, global_var_type,
                        resource_spec.resource_global_var());
                
                PCVariable * __global_pou_var = new PCVariable(__configuration,
                        this, "__RESOURCE_" + __ResourceName + "_GLOBAL_VAR__",
                        "__RESOURCE_" + __ResourceName + "_GLOBAL__");

                RegisterPoUVariable(
                    "__RESOURCE_" + __ResourceName + "_GLOBAL_VAR__",
                    __global_pou_var);

            }

            for (auto& pou_var : resource_spec.pou_var()) {

                assert(pou_var.datatype_category() == DataTypeCategory::POU);
                assert(pou_var.has_pou_type()
                    && (pou_var.pou_type() == PoUType::FC || 
                        pou_var.pou_type() == PoUType::FB ||
                        pou_var.pou_type() == PoUType::PROGRAM));
                         
                PCDataType * new_var_type = new PCDataType(
                    __configuration, 
                    pou_var.name(),
                    pou_var.name(),
                    DataTypeCategory::POU);

                __configuration->RegisteredDataTypes.RegisterDataType(
                                            pou_var.name(), new_var_type);

                Utils::InitializeDataType(__configuration, new_var_type,
                                        pou_var);

                
                PCVariable * new_pou_var = new PCVariable(
                    __configuration,
                    this, pou_var.name(), pou_var.name());
                RegisterPoUVariable(pou_var.name(), new_pou_var);

                if (pou_var.pou_type() == PoUType::FC)
                    new_pou_var->__VariableDataType->__PoUType = PoUType::FC;
                else if (pou_var.pou_type() == PoUType::FB)
                    new_pou_var->__VariableDataType->__PoUType = PoUType::FB;
                else
                    new_pou_var->__VariableDataType->__PoUType = PoUType::PROGRAM;
            }

            for(auto it = __ResourcePoUVars.begin();
                    it != __ResourcePoUVars.end(); it ++) {
                PCVariable * pou_var = it->second;
                pou_var->AllocateAndInitialize();
            
                Utils::ValidatePOUDefinition(pou_var, __configuration);
            }

            for(auto it = __ResourcePoUVars.begin();
                    it != __ResourcePoUVars.end(); it ++) {
                PCVariable * pou_var = it->second;

                if (pou_var->__VariableDataType->__PoUType 
                    == pc_specification::PoUType::PROGRAM)
                    pou_var->ResolveAllExternalFields(); // First resolve external fields in all programs
            }

            for(auto it = __ResourcePoUVars.begin();
                    it != __ResourcePoUVars.end(); it ++) {
                PCVariable * pou_var = it->second;

                if (pou_var->__VariableDataType->__PoUType 
                    == pc_specification::PoUType::FB)
                    pou_var->ResolveAllExternalFields(); //  Next resolve external fields in all Function Blocks
            }
            // No need to do this for FCs because they wouldn't have any external fields

            break;
        }
    }

}

PCVariable * PCResource::GetVariablePointerToMem(int memType, int ByteOffset,
                        int BitOffset, string VariableDataTypeName) {

    assert(ByteOffset > 0 && BitOffset >= 0 && BitOffset < 8);
    assert(memType == MemType::INPUT_MEM || memType == MemType::OUTPUT_MEM);
    string VariableName = __ResourceName 
                            + "." + VariableDataTypeName
                            + "." + std::to_string(memType)
                            + "." + std::to_string(ByteOffset)
                            + "." + std::to_string(BitOffset);

    // need to track and delete this variable later on
    auto got = __AccessedFields.find(VariableName);

    if(got == __AccessedFields.end()) {
        PCVariable* V = new PCVariable(__configuration, this, VariableName,
                                    VariableDataTypeName);
        assert(V != nullptr);

        if(memType == MemType::INPUT_MEM)
            V->__MemoryLocation.SetMemUnitLocation(&__InputMemory);
        else 
            V->__MemoryLocation.SetMemUnitLocation(&__OutputMemory);

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

void PCResource::Cleanup() {
    for ( auto it = __AccessedFields.begin(); it != __AccessedFields.end(); 
            ++it ) {
            PCVariable * __AccessedVariable = it->second;
            __AccessedVariable->Cleanup();
            delete __AccessedVariable;
    }

    for ( auto it = __ResourcePoUVars.begin(); it != __ResourcePoUVars.end(); 
            ++it ) {
            PCVariable * __AccessedVariable = it->second;
            __AccessedVariable->Cleanup();
            delete __AccessedVariable;
    }
}

PoUCodeContainer * PCResource::CreateNewCodeContainer(string PoUDataTypeName) {
    PCDataType * PoUDataType 
        = __configuration->RegisteredDataTypes.GetDataType(PoUDataTypeName);
    if (!PoUDataType)
        return nullptr;
    
    if (__CodeContainers.find(PoUDataTypeName) != __CodeContainers.end()) {
        __configuration->PCLogger->RaiseException("Cannot define two code "
                                        "bodies for same POU");
    }
    PoUCodeContainer * new_container = new PoUCodeContainer(__configuration,
                                                this);
    new_container->SetPoUDataType(PoUDataType);

    __CodeContainers.insert(std::make_pair(PoUDataTypeName, new_container));

    return new_container;

    
}
PoUCodeContainer * PCResource::GetCodeContainer(string PoUDataTypeName) {

    auto got = __CodeContainers.find(PoUDataTypeName);
    if (got == __CodeContainers.end())
        return nullptr;
    
    return got->second;
}


void PCResource::AddTask(Task * Tsk) {

    if (Tsk && __Tasks.find(Tsk->__TaskName) == __Tasks.end()) {
        __Tasks.insert(std::make_pair(Tsk->__TaskName, Tsk));
        return;
    }

    __configuration->PCLogger->RaiseException("Couldn't add tsk to resource !");
}

Task * PCResource::GetTask(string TaskName) {
    auto got = __Tasks.find(TaskName);
    if (got != __Tasks.end())
        return got->second;
    return nullptr;
}

void PCResource::QueueTask (Task * Tsk) {

    if (Tsk == nullptr)
        __configuration->PCLogger->RaiseException("Cannot queue null task!");

    auto got = __IntervalTasksByPriority.find(Tsk->__priority);
    if (got == __IntervalTasksByPriority.end() 
                    && Tsk->type == TaskType::INTERVAL) {
        
        CompactTaskDescription tskDescription(Tsk->__TaskName,
                                            Tsk->__nxt_schedule_time_ms);
        __IntervalTasksByPriority.insert(std::make_pair(Tsk->__priority,
                    priority_queue<CompactTaskDescription>()));
        __IntervalTasksByPriority.find(Tsk->__priority)->second.push(
                            tskDescription);
    } else if (Tsk->type == TaskType::INTERVAL) {
        CompactTaskDescription tskDescription(Tsk->__TaskName,
                                            Tsk->__nxt_schedule_time_ms);
        got->second.push(tskDescription);
    } else {
        auto got = __InterruptTasks.find(Tsk->__trigger_variable_name);
        if (got == __InterruptTasks.end()) {
            
            __InterruptTasks.insert(std::make_pair(Tsk->__trigger_variable_name,
                                    vector<Task*>()));
            __InterruptTasks.find(
                    Tsk->__trigger_variable_name)->second.push_back(Tsk);
        } else {
            got->second.push_back(Tsk);
        }
    }
}

Task * PCResource::GetInterruptTaskToExecute() {
    Task * EligibleTask = nullptr;
    int highest_priority = 10000;

    for (auto it = __InterruptTasks.begin(); it != __InterruptTasks.end(); it++) {
        PCVariable * trigger;
        trigger = __configuration->GetVariable(it->first);
        if(!trigger) {
            trigger = GetPOUGlobalVariable(it->first);
            if(!trigger)
                __configuration->PCLogger->RaiseException("Invalid trigger: "
                        + it->first);

        }
        assert(trigger->__VariableDataType->__DataTypeCategory 
                    == DataTypeCategory::BOOL); //trigger must be a boolean
        auto curr_value = trigger->GetFieldValue<bool>("",
                                        DataTypeCategory::BOOL);
        
        
        for (int itt = 0; itt < (int)it->second.size(); itt++) {
            auto prev_value = it->second[itt]->__trigger_variable_previous_value;
            if (prev_value == false && curr_value == true) {
                if (it->second[itt]->__priority < highest_priority) {
                    highest_priority = it->second[itt]->__priority;
                    EligibleTask = it->second[itt];
                }

                it->second[itt]->__IsReady = true;
                it->second[itt]->__trigger_variable_previous_value = true;
            } else if (it->second[itt]->__IsReady == true) {
                if (it->second[itt]->__priority < highest_priority) {
                    highest_priority = it->second[itt]->__priority;
                    EligibleTask = it->second[itt];
                }
            }
        }
    }

    if (EligibleTask != nullptr)
        return EligibleTask;
    return nullptr;
}

Task * PCResource::GetIntervalTaskToExecuteAt(double schedule_time) {

    

    // Else we need to get highest priority expired interval task
    
    Task * EligibleTask = nullptr;
    int highest_priority = 10000;

    for(auto it = __IntervalTasksByPriority.begin(); 
            it != __IntervalTasksByPriority.end(); it++ ) {
        
        auto compact_tsk_description = it->second.top();

        if (it->first < highest_priority 
            && compact_tsk_description.__nxt_schedule_time_ms <= schedule_time) {
            highest_priority = it->first;
            EligibleTask = GetTask(compact_tsk_description.__TaskName);
        }
    }

    if (EligibleTask != nullptr) {
        __IntervalTasksByPriority.find(highest_priority)->second.pop();
        return EligibleTask;
    }

    return nullptr;

}

void PCResource::InitializeAllTasks() {
    for (auto resource_spec : 
           __configuration->__specification.machine_spec().resource_spec()) {
        if(__ResourceName == resource_spec.resource_name()) {
            for (auto task_spec : resource_spec.tasks()) {
                    Task * new_task = new Task(__configuration, this, task_spec);

                    if (new_task->type == TaskType::INTERVAL) {
                        new_task->SetNextScheduleTime(clock->GetCurrentTime()
                            + (float)new_task->__interval_ms);
                    }

                    AddTask(new_task);
                    QueueTask(new_task);
            }   

            for (auto program_spec : resource_spec.programs()) {
                Task * associated_task 
                            = GetTask(program_spec.task_name());
                if (associated_task != nullptr) {
                    associated_task->AddProgramToTask(program_spec);
                    
                }
                
            }
            break;
        }
    }
}

 void PCResource::InitializeClock() {
     if (__configuration->__specification.has_enable_kronos() && 
            __configuration->__specification.enable_kronos())
        clock = new Clock(true, this);
    else {
        clock = new Clock(false, this);
    }

}

void PCResource::OnStartup() {
    InitializeClock();
    InitializeAllTasks();
}


