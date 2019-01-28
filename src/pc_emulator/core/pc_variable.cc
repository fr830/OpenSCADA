#include <iostream>
#include <cstdint>
#include <vector>
#include <cstring>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include "src/pc_emulator/include/pc_variable.h"
#include "src/pc_emulator/include/pc_datatype.h"
#include "src/pc_emulator/include/pc_configuration.h"
#include "src/pc_emulator/include/pc_resource.h"
#include "src/pc_emulator/include/utils.h"
using namespace std;
using namespace pc_emulator;
using namespace pc_specification;
using MemType  = pc_specification::MemType;
using DataTypeCategory = pc_specification::DataTypeCategory;
using FieldIntfType = pc_specification::FieldInterfaceType;


PCVariable::PCVariable(PCConfiguration * configuration,
                PCResource * AssociatedResource,
                string VariableName,
                string VariableDataTypeName) {

    //by default treat variables as IN_OUT unless explicitly stated.
    __configuration = configuration;
    assert(__configuration != nullptr);
    __VariableDataType = __configuration->RegisteredDataTypes.GetDataType(
                                            VariableDataTypeName);
    if (!__VariableDataType) {
        __configuration->PCLogger->RaiseException("Data type " 
                + VariableDataTypeName + " not registered !");
    }

    __AssociatedResource = AssociatedResource;
    __ByteOffset = 0;
    __BitOffset = 0;
    __VariableName = VariableName;
    //__MemoryLocation = PCMemUnit();
    __MemAllocated = false;
    __IsDirectlyRepresented = false;
}

void PCVariable::Cleanup() {

    // delete all accessed fields
    for ( auto it = __AccessedFields.begin(); it != __AccessedFields.end(); 
            ++it ) {
            PCVariable * __AccessedVariable = it->second;
            __AccessedVariable->Cleanup();
            delete __AccessedVariable;
    }   
}

void PCVariable::CheckValidity() {
    if (!__IsDirectlyRepresented)
        assert(__BitOffset == 0);
}

void PCVariable::AllocateStorage() {

    if (! __MemoryLocation.IsInitialized()) {
        int DataTypeSizeBytes = (__VariableDataType->__SizeInBits)/8;

        DataTypeSizeBytes = DataTypeSizeBytes > 0 ? DataTypeSizeBytes : 1;

        __MemoryLocation.AllocateStaticMemory(DataTypeSizeBytes);
    }
}

void PCVariable::AllocateAndInitialize() {

    if (!__MemAllocated) {
        this->AllocateStorage();
        this->InitializeAllNonPtrFields();
        this->InitializeAllDirectlyRepresentedFields();
    }
    __MemAllocated = true;
    CheckValidity();
}

void PCVariable::OnExecutorStartup() {
    for(auto& DefinedField: 
        __VariableDataType->__FieldsByInterfaceType[
            FieldIntfType::VAR_TEMP]) {
        
        PCVariable* FieldVariable 
            = GetPCVariableToField(DefinedField.__FieldName);
        InitializeVariable(FieldVariable);
    }
}

void PCVariable::ParseRemFieldAttributes(std::vector<string>& NestedFields,
                        int StartPos, DataTypeFieldAttributes& FieldAttributes,
                        PCVariable * HolderVariable) {
    
    if (StartPos == NestedFields.size())
        return;

    PCDataType * DataType = HolderVariable->__VariableDataType;
    FieldAttributes.FieldDataTypePtr = HolderVariable->__VariableDataType;
    FieldAttributes.HoldVariablePtr = HolderVariable;
    FieldAttributes.SizeInBits = HolderVariable->__VariableDataType->__SizeInBits;

    

    for (int i = StartPos; i < NestedFields.size(); i++) {
        string AccessedFieldName = NestedFields[i];
        for (int IntfType = FieldIntfType::VAR_INPUT; 
            IntfType != FieldIntfType::NA + 1; IntfType ++) {
            for(auto& DefinedField: DataType->__FieldsByInterfaceType[IntfType]) {
                PCDataType * FieldDataType = DefinedField.__FieldTypePtr;
                assert(FieldDataType != nullptr);
                if(AccessedFieldName == DefinedField.__FieldName) {
                    DataType = FieldDataType;
                    FieldAttributes.FieldInterfaceType = IntfType;
                    FieldAttributes.FieldDataTypePtr = DataType;

                    if (i == NestedFields.size() - 1)
                        return; // note theat the relative offset is already set

                    if (IntfType == FieldIntfType::VAR_IN_OUT || 
                        IntfType == FieldIntfType::VAR_EXTERNAL ||
                        IntfType == FieldIntfType::VAR_ACCESS ) {
                        PCVariable * nxtHolderVariable;
                                              
                        std::memcpy(&nxtHolderVariable,
                            HolderVariable->__MemoryLocation.GetPointerToMemory(
                            FieldAttributes.RelativeOffset),
                            sizeof(PCVariable *));
                        FieldAttributes.HoldVariablePtr = nxtHolderVariable;
                        FieldAttributes.RelativeOffset = 0; // Relative Offset reset                        
                        ParseRemFieldAttributes(NestedFields, i+1,
                                    FieldAttributes, nxtHolderVariable);
                        return;
                    } 
                    ParseRemFieldAttributes(NestedFields, i+1,
                                    FieldAttributes, HolderVariable);
                    return;
                } else {
                    if (IntfType == FieldIntfType::VAR_IN_OUT || 
                        IntfType == FieldIntfType::VAR_EXTERNAL ||
                        IntfType == FieldIntfType::VAR_ACCESS ) {
                        // this is a pointer
                        FieldAttributes.RelativeOffset += sizeof (PCDataType *);
                    } else {
                        FieldAttributes.RelativeOffset 
                                += FieldDataType->__SizeInBits;
                    }
                }
            }
        }

    }

    __configuration->PCLogger->RaiseException("Nested Field not found !");

}

void PCVariable::GetFieldAttributes(string NestedFieldName,
                                    DataTypeFieldAttributes& FieldAttributes) {
    std::vector<std::string> NestedFields;
    boost::split(NestedFields, NestedFieldName,
                boost::is_any_of("."), boost::token_compress_on);
    if (NestedFields.empty()) {
        
        FieldAttributes.RelativeOffset = 0;
        FieldAttributes.FieldInterfaceType = FieldIntfType::NA;
        FieldAttributes.SizeInBits = this->__VariableDataType->__SizeInBits;
        FieldAttributes.FieldDataTypePtr = this->__VariableDataType;
        FieldAttributes.NestedFieldName = "";
        FieldAttributes.HoldVariablePtr = this;
        return;
            
    }
    FieldAttributes.FieldDataTypePtr = this->__VariableDataType;
    FieldAttributes.HoldVariablePtr = this;
    FieldAttributes.FieldInterfaceType = FieldIntfType::NA;
    FieldAttributes.NestedFieldName = NestedFieldName;
    FieldAttributes.RelativeOffset = 0;
    FieldAttributes.SizeInBits = this->__VariableDataType->__SizeInBits;
    ParseRemFieldAttributes(NestedFields, 0, FieldAttributes, this);
}

PCVariable* PCVariable::GetPCVariableToField(string NestedFieldName) {
    DataTypeFieldAttributes Attributes;
    
    CheckValidity();
    assert(__VariableDataType->IsFieldPresent(NestedFieldName) == true);

    if (NestedFieldName.empty())
        return this;
    GetFieldAttributes(NestedFieldName, Attributes);

    PCVariable * HolderVariable = Attributes.HoldVariablePtr;

    if (Attributes.FieldInterfaceType != FieldIntfType::VAR_IN_OUT
        && Attributes.FieldInterfaceType != FieldIntfType::VAR_EXTERNAL
        && Attributes.FieldInterfaceType != FieldIntfType::VAR_ACCESS
        && Attributes.FieldInterfaceType != FieldIntfType::VAR_EXPLICIT_STORAGE) {

        std::unordered_map<std::string, PCVariable*>::const_iterator got = 
                        __AccessedFields.find (NestedFieldName);
        if (got != __AccessedFields.end()) {
            got->second->Cleanup();
            delete got->second;
        }

        PCVariable *VariablePtrToField
                                = new PCVariable(__configuration,
                                __AssociatedResource,
                                __VariableName + NestedFieldName,
                                Attributes.FieldDataTypePtr->__DataTypeName);
        VariablePtrToField->__ByteOffset 
                        = HolderVariable->__ByteOffset 
                        + Attributes.RelativeOffset / 8;
        VariablePtrToField->__BitOffset 
                        = HolderVariable->__BitOffset 
                        + (Attributes.RelativeOffset % 8);
        VariablePtrToField->__MemoryLocation.SetMemUnitLocation(
                            (PCMemUnit *)&HolderVariable->__MemoryLocation);
        VariablePtrToField->__MemAllocated = true;
        __AccessedFields.insert(
                        std::make_pair(NestedFieldName, VariablePtrToField));
        return VariablePtrToField;
        
        
    } else {
        PCVariable * VariablePtrToField;
        char * PtrToStorageLoc 
            = (char *) HolderVariable->__MemoryLocation.GetPointerToMemory(
                                HolderVariable->__ByteOffset 
                                + (Attributes.RelativeOffset / 8));
        memcpy(&VariablePtrToField, PtrToStorageLoc, sizeof(PCVariable *));
        return VariablePtrToField;
    }
}

void PCVariable::InitializeVariable(PCVariable * V) {

    assert(V->__VariableDataType != nullptr);

    switch(V->__VariableDataType->__DataTypeCategory) {
        case DataTypeCategory::BOOL : 
                        assert(V->__VariableDataType->__NFields == 0);
                        bool resbool;
                        assert(DataTypeUtils::ValueToBool(
                            V->__VariableDataType->__InitialValue, resbool));
                        V->SetPCVariableField("", &resbool, sizeof(resbool));
                        return;

        case DataTypeCategory::BYTE : 
                        assert(V->__VariableDataType->__NFields == 0);
                        int8_t resbyte;
                        assert(DataTypeUtils::ValueToByte(
                            V->__VariableDataType->__InitialValue, resbyte));
                        V->SetPCVariableField("", &resbyte, sizeof(resbyte));
                        return; 

        case DataTypeCategory::WORD : 
                assert(V->__VariableDataType->__NFields == 0);
                int16_t resword;
                assert(DataTypeUtils::ValueToWord(
                    V->__VariableDataType->__InitialValue, resword));
                V->SetPCVariableField("", &resword, sizeof(resword));
                return;     

        case DataTypeCategory::DWORD : 
                assert(V->__VariableDataType->__NFields == 0);
                int32_t resdword;
                assert(DataTypeUtils::ValueToDWord(
                    V->__VariableDataType->__InitialValue, resdword));
                V->SetPCVariableField("", &resdword, sizeof(resdword));
                return;     

        case DataTypeCategory::LWORD : 
                assert(V->__VariableDataType->__NFields == 0);
                int64_t reslword;
                assert(DataTypeUtils::ValueToLWord(
                    V->__VariableDataType->__InitialValue, reslword));
                V->SetPCVariableField("", &reslword, sizeof(reslword));
                return;     

        case DataTypeCategory::CHAR : 
                assert(V->__VariableDataType->__NFields == 0);
                char reschar;
                assert(DataTypeUtils::ValueToChar(
                    V->__VariableDataType->__InitialValue, reschar));
                V->SetPCVariableField("", &reschar, sizeof(reschar));
                return; 
                
        case DataTypeCategory::INT : 
                assert(V->__VariableDataType->__NFields == 0);
                int16_t resint;
                assert(DataTypeUtils::ValueToInt(
                    V->__VariableDataType->__InitialValue, resint));
                V->SetPCVariableField("", &resint, sizeof(resint));
                return;

        case DataTypeCategory::SINT : 
                assert(V->__VariableDataType->__NFields == 0);
                int8_t ressint;
                assert(DataTypeUtils::ValueToSint(
                    V->__VariableDataType->__InitialValue, ressint));
                V->SetPCVariableField("", &ressint, sizeof(ressint));
                return;     

        case DataTypeCategory::DINT : 
                assert(V->__VariableDataType->__NFields == 0);
                int32_t resdint;
                assert(DataTypeUtils::ValueToDint(
                    V->__VariableDataType->__InitialValue, resdint));
                V->SetPCVariableField("", &resdint, sizeof(resdint));
                return;     

        case DataTypeCategory::LINT : 
                assert(V->__VariableDataType->__NFields == 0);
                int64_t reslint;
                assert(DataTypeUtils::ValueToLint(
                    V->__VariableDataType->__InitialValue, reslint));
                V->SetPCVariableField("", &reslint, sizeof(reslint));
                return;     

        case DataTypeCategory::UINT : 
                assert(V->__VariableDataType->__NFields == 0);
                uint16_t resuint;
                assert(DataTypeUtils::ValueToUint(
                    V->__VariableDataType->__InitialValue, resuint));
                V->SetPCVariableField("", &resuint, sizeof(resuint));
                return;     

        case DataTypeCategory::USINT : 
                assert(V->__VariableDataType->__NFields == 0);
                uint8_t resusint;
                assert(DataTypeUtils::ValueToUsint(
                    V->__VariableDataType->__InitialValue, resusint));
                V->SetPCVariableField("", &resusint, sizeof(resusint));
                return;     

        case DataTypeCategory::UDINT : 
                assert(V->__VariableDataType->__NFields == 0);
                uint32_t resudint;
                assert(DataTypeUtils::ValueToUdint(
                    V->__VariableDataType->__InitialValue, resudint));
                V->SetPCVariableField("", &resudint, sizeof(resudint));
                return;

        case DataTypeCategory::ULINT : 
                assert(V->__VariableDataType->__NFields == 0);
                uint64_t resulint;
                assert(DataTypeUtils::ValueToUlint(
                    V->__VariableDataType->__InitialValue, resulint));
                V->SetPCVariableField("", &resulint, sizeof(resulint));
                return; 

        case DataTypeCategory::REAL : 
                assert(V->__VariableDataType->__NFields == 0);
                float resreal;
                assert(DataTypeUtils::ValueToReal(
                    V->__VariableDataType->__InitialValue, resreal));
                V->SetPCVariableField("", &resreal, sizeof(resreal));
                return;     

        case DataTypeCategory::LREAL : 
                assert(V->__VariableDataType->__NFields == 0);
                double reslreal;
                assert(DataTypeUtils::ValueToLReal(
                    V->__VariableDataType->__InitialValue, reslreal));
                V->SetPCVariableField("", &reslreal, sizeof(reslreal));
                return;     

        case DataTypeCategory::TIME : 
                assert(V->__VariableDataType->__NFields == 0);
                TimeType restime;
                assert(DataTypeUtils::ValueToTime(
                    V->__VariableDataType->__InitialValue, restime));
                V->SetPCVariableField("", &restime, sizeof(restime));
                return;     

        case DataTypeCategory::DATE : 
                assert(V->__VariableDataType->__NFields == 0);
                DateType resdate;
                assert(DataTypeUtils::ValueToDate(
                    V->__VariableDataType->__InitialValue, resdate));
                V->SetPCVariableField("", &resdate, sizeof(resdate));
                return;     

        case DataTypeCategory::DATE_AND_TIME : 
                assert(V->__VariableDataType->__NFields == 0);
                DateTODType resdt;
                assert(DataTypeUtils::ValueToDT(
                    V->__VariableDataType->__InitialValue, resdt));
                V->SetPCVariableField("", &resdt, sizeof(resdt));
                return;     

        case DataTypeCategory::TIME_OF_DAY : 
                assert(V->__VariableDataType->__NFields == 0);
                TODType restod;
                assert(DataTypeUtils::ValueToTOD(
                    V->__VariableDataType->__InitialValue, restod));
                V->SetPCVariableField("", &restod, sizeof(restod));
                return;     

        default :
                for (int IntfType = FieldIntfType::VAR_INPUT; 
                    IntfType != FieldIntfType::NA + 1; IntfType ++) {
                    
                    if (IntfType != FieldIntfType::VAR_IN_OUT &&
                        IntfType != FieldIntfType::VAR_EXTERNAL &&
                        IntfType != FieldIntfType::VAR_ACCESS &&
                        IntfType != FieldIntfType::VAR_EXPLICIT_STORAGE) {
                        // all non ptr fields
                        for(auto& DefinedField: 
                            V->__VariableDataType->__FieldsByInterfaceType[IntfType]) {
                            
                            PCVariable* FieldVariable 
                                = V->GetPCVariableToField(
                                 DefinedField.__FieldName);
                            InitializeVariable(FieldVariable);
                        }
                    } // no need to initialize explit storage type subfields here
                      // because there would be none. The variable V or any of
                      // its nested fields will not be explicit storage type
                }
                return; 
    }

    return;

}
void PCVariable::InitializeAllNonPtrFields() {
    for (int IntfType = FieldIntfType::VAR_INPUT; 
            IntfType != FieldIntfType::NA; IntfType ++) {
        
        if (IntfType != FieldIntfType::VAR_IN_OUT &&
            IntfType != FieldIntfType::VAR_EXTERNAL &&
            IntfType != FieldIntfType::VAR_ACCESS &&
            IntfType != FieldIntfType::VAR_EXPLICIT_STORAGE) {
            for(auto& DefinedField: 
                __VariableDataType->__FieldsByInterfaceType[IntfType]) {
                
                PCVariable* FieldVariable 
                    = GetPCVariableToField(DefinedField.__FieldName);

                // Note that no subfield of this can be of explicit storage type
                // because this field is not explicit storage type.
                InitializeVariable(FieldVariable);
            }
        } 
    }
}

void PCVariable::InitializeAllDirectlyRepresentedFields() {
    if (__VariableDataType->__DataTypeCategory == DataTypeCategory::POU) {
        for(auto& DefinedField: 
                __VariableDataType->__FieldsByInterfaceType[
                    FieldIntfType::VAR_EXPLICIT_STORAGE]) {
                
                PCVariable* FieldVariable;  
                if (DefinedField.__StorageMemType == MemType::RAM_MEM)          
                    FieldVariable 
                        = __configuration->GetVariablePointerToMem(
                            DefinedField.__StorageMemType,
                            DefinedField.__StorageByteOffset,
                            DefinedField.__StorageBitOffset,
                            DefinedField.__FieldTypeName);
                else {
                    assert(__AssociatedResource != nullptr);
                    FieldVariable 
                        = __AssociatedResource->GetVariablePointerToMem(
                            DefinedField.__StorageMemType,
                            DefinedField.__StorageByteOffset,
                            DefinedField.__StorageBitOffset,
                            DefinedField.__FieldTypeName);
                }

                //InitializeVariable(FieldVariable);
                SetPtr(DefinedField.__FieldName, FieldVariable);
        }
    }
}

void PCVariable::ResolveAllExternalFields() {
    if (__VariableDataType->__DataTypeCategory == DataTypeCategory::POU) {
        for(auto& DefinedField: 
                __VariableDataType->__FieldsByInterfaceType[
                    FieldIntfType::VAR_EXTERNAL]) {
                
                PCVariable* FieldVariable;  
                          
                FieldVariable 
                    = __configuration->GetVariable(DefinedField.__FieldName);
                if (FieldVariable == nullptr 
                    && __AssociatedResource != nullptr) {
                    FieldVariable = __AssociatedResource->GetGlobalVariable(
                                        DefinedField.__FieldName);

                    if (FieldVariable == nullptr) {
                        __configuration->PCLogger->RaiseException(
                                "Cannot resolve an external field: "
                                + DefinedField.__FieldName);
                    }
                }
                
                SetPtr(DefinedField.__FieldName, FieldVariable);
        }
    }
}


void PCVariable::SetPtr(string NestedFieldName, PCVariable * ptr) {
    assert(__VariableDataType->__DataTypeCategory 
                            == DataTypeCategory::POU);
    assert(!NestedFieldName.empty() && ptr != nullptr);
    assert(__VariableDataType->IsFieldPresent(NestedFieldName) == true);
    CheckValidity();
    
    DataTypeFieldAttributes Attributes;
    GetFieldAttributes(NestedFieldName, Attributes);

    assert(Attributes.FieldDataTypePtr->__DataTypeName
            == ptr->__VariableDataType->__DataTypeName);
    assert(Attributes.FieldInterfaceType == FieldIntfType::VAR_IN_OUT ||
        Attributes.FieldInterfaceType == FieldIntfType::VAR_EXPLICIT_STORAGE ||
        Attributes.FieldInterfaceType == FieldIntfType::VAR_ACCESS ||
        Attributes.FieldInterfaceType == FieldIntfType::VAR_EXTERNAL);
    CopyPCVariableFieldFromPointer(Attributes, ptr);
}

PCVariable * PCVariable::GetPtrStoredAtField(string NestedFieldName) {
    DataTypeFieldAttributes Attributes;
    GetFieldAttributes(NestedFieldName, Attributes);

    if(Attributes.FieldInterfaceType == FieldIntfType::VAR_IN_OUT ||
        Attributes.FieldInterfaceType == FieldIntfType::VAR_EXPLICIT_STORAGE ||
        Attributes.FieldInterfaceType == FieldIntfType::VAR_ACCESS ||
        Attributes.FieldInterfaceType == FieldIntfType::VAR_EXTERNAL) {
        PCVariable *StoredPointer;
        auto FieldVariable = GetPCVariableToField(NestedFieldName);
        std::memcpy(&StoredPointer, 
            FieldVariable->__MemoryLocation.GetPointerToMemory(FieldVariable->__ByteOffset),
            sizeof(PCVariable *));
        return StoredPointer;
    }

    return nullptr; // this field location does not store a pointer

    

}

void PCVariable::CopyPCVariableFieldFromPointer(
        DataTypeFieldAttributes& Attributes, PCVariable * From) {
    assert(Attributes.FieldDataTypePtr->__DataTypeName
            == From->__VariableDataType->__DataTypeName);

    PCVariable * FieldVariable = 
                    GetPCVariableToField(Attributes.NestedFieldName);

    if (Attributes.FieldInterfaceType != FieldIntfType::VAR_IN_OUT
        && Attributes.FieldInterfaceType != FieldIntfType::VAR_EXTERNAL
        && Attributes.FieldInterfaceType != FieldIntfType::VAR_ACCESS
        && Attributes.FieldInterfaceType != FieldIntfType::VAR_EXPLICIT_STORAGE) {
            // The content pointed to by From is copied at the appropriate
            // offset
            if (From->__VariableDataType->__DataTypeCategory 
                                            == DataTypeCategory::BOOL) {

                int8_t temp = From->__MemoryLocation.GetStorageLocation()
                                                    .get()[From->__ByteOffset];
                if (temp & ((1UL) <<  From->__BitOffset)) {
                    FieldVariable->__MemoryLocation.GetStorageLocation().get()[
                        FieldVariable->__ByteOffset] 
                                |= ((1UL) <<  FieldVariable->__BitOffset);
                }
                else {
                    FieldVariable->__MemoryLocation.GetStorageLocation().get()[
                        FieldVariable->__ByteOffset] 
                                &= ~((1UL) <<  FieldVariable->__BitOffset);
                }
                return;
            }
        

            
            FieldVariable->__MemoryLocation.CopyFromMemUnit(
                &From->__MemoryLocation, 
                From->__ByteOffset,
                From->__MemoryLocation.GetMemUnitSize() - From->__ByteOffset,
                FieldVariable->__ByteOffset);


    } else {
        // The pointer From is itself to be copied at the appropriate offset
        assert(FieldVariable->__ByteOffset  + sizeof(PCVariable *) 
            <= FieldVariable->__MemoryLocation.GetMemUnitSize());

        std::memcpy(
            FieldVariable->__MemoryLocation.GetPointerToMemory(
                FieldVariable->__ByteOffset), &From, sizeof(PCVariable *));
    }
}

void PCVariable::CopyPCVariableFieldFromPointer(string NestedFieldName,
                PCVariable * From) {
    
    
    if (NestedFieldName.empty()) {
        //  Copy whole content of From except variable name.
        assert(this->__VariableDataType->__DataTypeName
            == From->__VariableDataType->__DataTypeName);


        if (From->__VariableDataType->__DataTypeCategory 
                                        == DataTypeCategory::BOOL) {

            int8_t temp = From->__MemoryLocation.GetStorageLocation().get()
                                                        [From->__ByteOffset];
            if (temp & ((1UL) <<  From->__BitOffset)) {
                this->__MemoryLocation.GetStorageLocation().get()[
                    __ByteOffset] |= ((1UL) <<  __BitOffset);
            }
            else {
                this->__MemoryLocation.GetStorageLocation().get()[
                    __ByteOffset] &= ~((1UL) <<  __BitOffset);
            }
            return;
        }

        this->__MemoryLocation.CopyFromMemUnit(&From->__MemoryLocation, 
                From->__ByteOffset,
                From->__MemoryLocation.GetMemUnitSize() - From->__ByteOffset,
                __ByteOffset);
        return;

    }

    DataTypeFieldAttributes Attributes;
    GetFieldAttributes(NestedFieldName, Attributes);

    CopyPCVariableFieldFromPointer(Attributes, From);
}



void PCVariable::SetPCVariableField(string NestedFieldName, string Value) {

    DataTypeFieldAttributes Attributes;
    assert(__VariableDataType->IsFieldPresent(NestedFieldName) == true);
    GetFieldAttributes(NestedFieldName, Attributes);
    CheckValidity();


    if (Attributes.FieldInterfaceType == FieldIntfType::VAR_IN_OUT
        || Attributes.FieldInterfaceType == FieldIntfType::VAR_EXTERNAL
        || Attributes.FieldInterfaceType == FieldIntfType::VAR_ACCESS) {

       return; // a string value cannot be set at a field location which is a pointer, use SetPtr instead
    }

    if (Attributes.FieldInterfaceType 
                == FieldIntfType::VAR_EXPLICIT_STORAGE) {
        auto FieldLocationPtr = GetPCVariableToField(NestedFieldName);
        // now we must get the pointer value at this field location

        PCVariable * ActualPointerVariable;
        std::memcpy(&ActualPointerVariable,
                FieldLocationPtr->__MemoryLocation
                    .GetPointerToMemory(FieldLocationPtr->__ByteOffset),
                sizeof(PCVariable *));
        return ActualPointerVariable->SetPCVariableField("", Value);

    } // this case needs to be handled separately, we must get value of pointer at
      // field location

    assert(this->__MemoryLocation.IsInitialized());
    PCVariable *FieldVariable = 
                GetPCVariableToField(NestedFieldName);

    switch(Attributes.FieldDataTypePtr->__DataTypeCategory) {
        case DataTypeCategory::BOOL :     
            bool BoolValue;
            
            int bit_off = FieldVariable->__BitOffset;
            if (!DataTypeUtils::ValueToBool(Value, BoolValue)){
                __configuration->PCLogger->RaiseException(
                    "Bool conversion error !");
            }
            
            if (BoolValue) // set bit at bit_offset
                FieldVariable->__MemoryLocation.GetStorageLocation().get()[
                    FieldVariable->__ByteOffset] |= 
                                ((1UL) <<  bit_off);
            else // clear bit at bit offset
                FieldVariable->__MemoryLocation.GetStorageLocation().get()[
                    FieldVariable->__ByteOffset] &= 
                                ~((1UL) <<  bit_off);
            break;

        case DataTypeCategory::BYTE :     
            int8_t ByteValue;
            if (!DataTypeUtils::ValueToByte(Value, ByteValue)){
                __configuration->PCLogger->RaiseException(
                    "Byte conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &ByteValue, sizeof(int8_t));
            break;
        case DataTypeCategory::WORD :     
            int16_t WordValue;
            if (!DataTypeUtils::ValueToWord(Value, WordValue)){
                __configuration->PCLogger->RaiseException(
                    "Word conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &WordValue, sizeof(int16_t));
            break;
        case DataTypeCategory::DWORD :     
            int32_t DWordValue;
            if (!DataTypeUtils::ValueToDWord(Value, DWordValue)){
                __configuration->PCLogger->RaiseException(
                    "DWord conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &DWordValue, sizeof(int32_t));
            break;
        case DataTypeCategory::LWORD :    
            int64_t LWordValue;
            if (!DataTypeUtils::ValueToLWord(Value, LWordValue)){
                __configuration->PCLogger->RaiseException(
                    "LWord conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &LWordValue, sizeof(int64_t));
            break;
        case DataTypeCategory::CHAR :    
            char CharValue;
            if (!DataTypeUtils::ValueToChar(Value, CharValue)){
                __configuration->PCLogger->RaiseException(
                    "Char conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &CharValue, sizeof(char));
            break;
        case DataTypeCategory::INT :      
            int16_t IntValue;
            if (!DataTypeUtils::ValueToInt(Value, IntValue)){
                __configuration->PCLogger->RaiseException(
                    "Int conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &IntValue, sizeof(int16_t));
            break;
        case DataTypeCategory::SINT :     
            int8_t SIntValue;
            if (!DataTypeUtils::ValueToSint(Value, SIntValue)){
                __configuration->PCLogger->RaiseException(
                    "SInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &SIntValue, sizeof(int8_t));
            break;
        case DataTypeCategory::DINT :     
            int32_t DIntValue;
            if (!DataTypeUtils::ValueToDint(Value, DIntValue)){
                __configuration->PCLogger->RaiseException(
                    "DInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &DIntValue, sizeof(int32_t));
            break;
        case DataTypeCategory::LINT :     
            int64_t LIntValue;
            if (!DataTypeUtils::ValueToLint(Value, LIntValue)){
                __configuration->PCLogger->RaiseException(
                    "LInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &LIntValue, sizeof(int64_t));
            break;
        case DataTypeCategory::UINT :     
            uint16_t UIntValue;
            if (!DataTypeUtils::ValueToUint(Value, UIntValue)){
                __configuration->PCLogger->RaiseException(
                    "UInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &UIntValue, sizeof(uint16_t));
            break;
        case DataTypeCategory::USINT :     
            uint8_t USIntValue;
            if (!DataTypeUtils::ValueToUsint(Value, USIntValue)){
                __configuration->PCLogger->RaiseException(
                    "USInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &USIntValue, sizeof(uint8_t));
            break;
        case DataTypeCategory::UDINT :     
            uint32_t UDIntValue;
            if (!DataTypeUtils::ValueToUdint(Value, UDIntValue)){
                __configuration->PCLogger->RaiseException(
                    "UDInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &UDIntValue, sizeof(uint32_t));
            break;
        case DataTypeCategory::ULINT :     
            uint64_t ULIntValue;
            if (!DataTypeUtils::ValueToUlint(Value, ULIntValue)){
                __configuration->PCLogger->RaiseException(
                    "ULInt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &ULIntValue, sizeof(uint64_t));
            break;
        case DataTypeCategory::REAL :     
            float RealValue;
            if (!DataTypeUtils::ValueToReal(Value, RealValue)){
                __configuration->PCLogger->RaiseException(
                    "Real conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &RealValue, sizeof(float));
            break;
        case DataTypeCategory::LREAL :     
            double LRealValue;
            if (!DataTypeUtils::ValueToLReal(Value, LRealValue)){
                __configuration->PCLogger->RaiseException(
                    "LReal conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &LRealValue, sizeof(double));
            break;
        case DataTypeCategory::TIME :     
            TimeType Time;
            if (!DataTypeUtils::ValueToTime(Value, Time)){
                __configuration->PCLogger->RaiseException(
                    "Time conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &Time, sizeof(TimeType));
            break;
        case DataTypeCategory::DATE :     
            DateType Date;
            if (!DataTypeUtils::ValueToDate(Value, Date)){
                __configuration->PCLogger->RaiseException(
                    "Date conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &Date, sizeof(DateType));
            break;
        case DataTypeCategory::TIME_OF_DAY :     
            TODType TOD;
            if (!DataTypeUtils::ValueToTOD(Value, TOD)){
                __configuration->PCLogger->RaiseException(
                    "TOD conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &TOD, sizeof(TOD));
            break;
        case DataTypeCategory::DATE_AND_TIME :     
            DateTODDataType Dt;
            if (!DataTypeUtils::ValueToDT(Value, Dt)){
                __configuration->PCLogger->RaiseException(
                    "Dt conversion error !");
            }
            std::memcpy(
                FieldVariable->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
                &Dt, sizeof(DateTODDataType));
            break;
        default :           __configuration->PCLogger->RaiseException(
                                "Only fields pointing to elementary data types"
                                " can be set with passed string values !");
    }
}

void PCVariable::SetPCVariableField(string NestedFieldName, void * Value,
                                    int CopySizeBytes) {

    CheckValidity();
    assert(__VariableDataType->IsFieldPresent(NestedFieldName) == true);
    if (!NestedFieldName.empty()) {
        DataTypeFieldAttributes Attributes;
        GetFieldAttributes(NestedFieldName, Attributes);

        if (Attributes.FieldInterfaceType == FieldIntfType::VAR_IN_OUT
            || Attributes.FieldInterfaceType 
                        == FieldIntfType::VAR_EXTERNAL
            || Attributes.FieldInterfaceType 
                        == FieldIntfType::VAR_ACCESS
            || Attributes.FieldInterfaceType 
                        == FieldIntfType::VAR_EXPLICIT_STORAGE) {
                // it is a pointer, we must get the pointed variable and set it there
                auto PointedVariable  =  GetPCVariableToField(
                        NestedFieldName);       
                PointedVariable->SetPCVariableField("", &Value,
                                sizeof(PCVariable *)); // Value is set at appropriate offset, not *Value.
                return;
        } 

        assert(this->__MemoryLocation.IsInitialized());
        PCVariable *FieldVariable = 
                GetPCVariableToField(NestedFieldName);

        if (FieldVariable->__VariableDataType->__DataTypeCategory 
                                            == DataTypeCategory::BOOL) {
            bool BoolValue = *(bool *)Value;

            if (BoolValue) // set bit at appropriate bit and byte offset
                FieldVariable->__MemoryLocation.GetStorageLocation().get()[
                    FieldVariable->__ByteOffset] 
                        |= ((1UL) << FieldVariable->__BitOffset);
            else
                FieldVariable->__MemoryLocation.GetStorageLocation().get()[
                    FieldVariable->__ByteOffset] 
                        &= ~((1UL) << FieldVariable->__BitOffset);
        }

        std::memcpy(
            this->__MemoryLocation
                    .GetPointerToMemory(FieldVariable->__ByteOffset),
            Value, CopySizeBytes);
        return;

    }

    std::memcpy(this->__MemoryLocation.GetPointerToMemory(__ByteOffset),
                Value, CopySizeBytes);
    return;
}

void PCVariable::GetAndStoreValue(string NestedFieldName,
    void * Value, int CopySize, int CategoryOfDataType) {
    assert (Value != nullptr);
    assert(__VariableDataType->IsFieldPresent(NestedFieldName) == true);
    
    PCVariable* PointerAtFieldLocation = GetPtrStoredAtField(NestedFieldName);
    if (PointerAtFieldLocation != nullptr) {
        
        return PointerAtFieldLocation->GetAndStoreValue("", Value, CopySize,
                                                        CategoryOfDataType);

    } 

    auto PointedVariable =  GetPCVariableToField(NestedFieldName);
    assert(PointedVariable->__VariableDataType->__DataTypeCategory
        == CategoryOfDataType);

    if (CategoryOfDataType == DataTypeCategory::BOOL) {
        if (PointedVariable->__MemoryLocation.GetStorageLocation().get()[
            PointedVariable->__ByteOffset] 
            & ((1UL) << PointedVariable->__ByteOffset)) {
            // bit pointed to by pointed variable is set
            *(bool *) Value = true;
        } else {
            *(bool *) Value = false;
        }
        return;
    }
    std::memcpy(Value, 
        PointedVariable->__MemoryLocation.GetPointerToMemory(
            PointedVariable->__ByteOffset),  CopySize);    
}

template <typename T> T PCVariable::GetFieldValue(string NestedFieldName,
                                            int CategoryOfDataType) {
    T Value;
    int CopySize = sizeof(T);
    CheckValidity();
    GetAndStoreValue(NestedFieldName, &Value, CopySize, CategoryOfDataType);
    return Value;
}

void PCVariable::CheckOperationValidity(int CategoryOfDataType, int VarOp) {

    if (CategoryOfDataType == DataTypeCategory::NOT_ASSIGNED ||
        CategoryOfDataType == DataTypeCategory::DERIVED ||
        CategoryOfDataType == DataTypeCategory::POU ||
        CategoryOfDataType == DataTypeCategory::ARRAY)
            __configuration->PCLogger->RaiseException("Cannot perform operation"
                                    "on complex data type variables");
    switch(VarOp) {
        case VariableOps::ADD :     
            if (CategoryOfDataType == DataTypeCategory::DATE ||
                CategoryOfDataType == DataTypeCategory::DATE_AND_TIME)
                __configuration->PCLogger->RaiseException(
                    "Cannot perform + operation on date and date_time vars");
            break;
        
        case VariableOps::SUB :     
            if (CategoryOfDataType == DataTypeCategory::DATE ||
                CategoryOfDataType == DataTypeCategory::DATE_AND_TIME)
                __configuration->PCLogger->RaiseException(
                    "Cannot perform - operation on date and date_time vars");
            break;
        
        case VariableOps::MUL : 
        case VariableOps::DIV : 
        case VariableOps::MOD :
        case VariableOps::AND :
        case VariableOps::OR  :
        case VariableOps::XOR :
        case VariableOps::LS  :
        case VariableOps::RS  : 

            if (CategoryOfDataType == DataTypeCategory::DATE ||
                CategoryOfDataType == DataTypeCategory::DATE_AND_TIME ||
                CategoryOfDataType == DataTypeCategory::TIME_OF_DAY ||
                CategoryOfDataType == DataTypeCategory::TIME)
                __configuration->PCLogger->RaiseException(
                    "Cannot perform any of *,/,%%,&,|,^,<<,>> operation on "
                    "date, date_time, tod and time vars");
            break;           
            
    }

}

template <typename T> bool PCVariable::ArithmeticOpOnVariables(T var1, T var2,
                                        int CategoryOfDataType, int VarOp) {
     switch(VarOp) {
        case VariableOps::ADD :     
            auto resa = var1 + var2;
            this->SetPCVariableField("", &resa, sizeof(resa));
            return true;
        
        case VariableOps::SUB :     
            auto ress = var1 - var2;
            this->SetPCVariableField("", &ress, sizeof(ress));
            return true;
        
        case VariableOps::MUL : 
            auto resm = var1 * var2;
            this->SetPCVariableField("", &resm, sizeof(resm));
            return true;
        case VariableOps::DIV : 
            auto resd = var1/var2;
            this->SetPCVariableField("", &resd, sizeof(resd));
            return true;
        case VariableOps::MOD :
            auto resmod = var1 % var2;
            this->SetPCVariableField("", &resmod, sizeof(resmod));
            return true;
        default: __configuration->PCLogger->RaiseException("Unsupported "
                                    "variable arithmetic operation type !");
    }
    return false;
}

template <typename T> bool PCVariable::RelationalOpOnVariables(T var1, T var2,
                                int CategoryOfDataType, int VarOp) {
    switch(VarOp) {
        case VariableOps::EQ  :
            auto reseq = (var1 == var2);
            this->SetPCVariableField("", &reseq, sizeof(reseq));
            return reseq;
        case VariableOps::GT  :
            auto resgt = (var1 > var2);
            this->SetPCVariableField("", &resgt, sizeof(resgt));
            return resgt;      
        case VariableOps::GE  :
            auto resge = (var1 >= var2);
            this->SetPCVariableField("", &resge, sizeof(resge));
            return resge;
        case VariableOps::LT  :
            auto reslt = (var1 < var2);
            this->SetPCVariableField("", &reslt, sizeof(reslt));
            return reslt;
        case VariableOps::LE  :
            auto resle = (var1 <= var2);
            this->SetPCVariableField("", &resle, sizeof(resle));
            return resle;
        default :   __configuration->PCLogger->RaiseException("Unsupported "
                                    "variable relational operation type !");
    }
    return false;
}

template <typename T> bool PCVariable::BitwiseOpOnVariables(T var1, T var2,
                            int CategoryOfDataType, int VarOp) {
    switch(VarOp) {
        case VariableOps::OR  :
            auto resor = var1 | var2;
            this->SetPCVariableField("", &resor, sizeof(resor));
            return true;
        case VariableOps::XOR :
            auto resxor = var1 ^ var2;
            this->SetPCVariableField("", &resxor, sizeof(resxor));
            return true;
        case VariableOps::LS  :
            auto resls = var1 << var2;
            this->SetPCVariableField("", &resls, sizeof(resls));
            return true;
        case VariableOps::RS  :
            auto resrs = var1 >> var2;
            this->SetPCVariableField("", &resrs, sizeof(resrs));
            return true;

         default: __configuration->PCLogger->RaiseException("Unsupported "
                                    "variable bitwise operation type !");
    }
    return false;
}

template <typename T> bool PCVariable::AllOpsOnVariables(T var1, T var2,
                                        int CategoryOfDataType, int VarOp) {

    switch(VarOp) {
        case VariableOps::ADD :     
            auto resa = var1 + var2;
            this->SetPCVariableField("", &resa, sizeof(resa));
            return true;
        
        case VariableOps::SUB :     
            auto ress = var1 - var2;
            this->SetPCVariableField("", &ress, sizeof(ress));
            return true;
        
        case VariableOps::MUL : 
            auto resm = var1 * var2;
            this->SetPCVariableField("", &resm, sizeof(resm));
            return true;
        case VariableOps::DIV : 
            auto resd = var1/var2;
            this->SetPCVariableField("", &resd, sizeof(resd));
            return true;
        case VariableOps::MOD :
            auto resmod = var1 % var2;
            this->SetPCVariableField("", &resmod, sizeof(resmod));
            return true;
        case VariableOps::AND :
            auto resand = var1 & var2;
            this->SetPCVariableField("", &resand, sizeof(resand));
            return true;

        case VariableOps::OR  :
            auto resor = var1 | var2;
            this->SetPCVariableField("", &resor, sizeof(resor));
            return true;
        case VariableOps::XOR :
            auto resxor = var1 ^ var2;
            this->SetPCVariableField("", &resxor, sizeof(resxor));
            return true;
        case VariableOps::LS  :
            auto resls = var1 << var2;
            this->SetPCVariableField("", &resls, sizeof(resls));
            return true;
        case VariableOps::RS  :
            auto resrs = var1 >> var2;
            this->SetPCVariableField("", &resrs, sizeof(resrs));
            return true;

        case VariableOps::EQ  :
            auto reseq = (var1 == var2);
            this->SetPCVariableField("", &reseq, sizeof(reseq));
            return reseq;
        case VariableOps::GT  :
            auto resgt = (var1 > var2);
            this->SetPCVariableField("", &resgt, sizeof(resgt));
            return resgt;      
        case VariableOps::GE  :
            auto resge = (var1 >= var2);
            this->SetPCVariableField("", &resge, sizeof(resge));
            return resge;
        case VariableOps::LT  :
            auto reslt = (var1 < var2);
            this->SetPCVariableField("", &reslt, sizeof(reslt));
            return reslt;
        case VariableOps::LE  :
            auto resle = (var1 <= var2);
            this->SetPCVariableField("", &resle, sizeof(resle));
            return resle;
        default :   __configuration->PCLogger->RaiseException("Unsupported "
                                    "variable operation type !");

    }


    return false;
}

void PCVariable::operator=(PCVariable& V) {
    this->__ByteOffset = V.__ByteOffset;
    this->__BitOffset = V.__BitOffset;
    this->__VariableName = V.__VariableName;
    this->__VariableDataType = V.__VariableDataType;
    this->__MemoryLocation = V.__MemoryLocation;
    this->__configuration = V.__configuration;
    this->__MemAllocated = V.__MemAllocated;
    this->__IsDirectlyRepresented = V.__IsDirectlyRepresented;
    this->__AssociatedResource = V.__AssociatedResource;
}



bool PCVariable::InitiateOperationOnVariables(PCVariable& V, int VarOp) {

    assert(this->__VariableDataType->__DataTypeCategory
        == V.__VariableDataType->__DataTypeCategory);
    int CategoryOfDataType = V.__VariableDataType->__DataTypeCategory;
    CheckValidity();

    CheckOperationValidity(CategoryOfDataType, VarOp);
    auto varoptype = Utils::GetVarOpType(VarOp);

    switch(CategoryOfDataType) {
        case DataTypeCategory::BOOL :
            return AllOpsOnVariables<bool>(
                    this->GetFieldValue<bool>("", CategoryOfDataType),
                    V.GetFieldValue<bool>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);

        case DataTypeCategory::BYTE :
        case DataTypeCategory::SINT :     
            return AllOpsOnVariables<int8_t>(
                    this->GetFieldValue<int8_t>("", CategoryOfDataType),
                    V.GetFieldValue<int8_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
        
        case DataTypeCategory::WORD :   
        case DataTypeCategory::INT :     
            return AllOpsOnVariables<int16_t>(
                    this->GetFieldValue<int16_t>("", CategoryOfDataType),
                    V.GetFieldValue<int16_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);

        case DataTypeCategory::DWORD :   
        case DataTypeCategory::DINT :     
            return AllOpsOnVariables<int32_t>(
                    this->GetFieldValue<int32_t>("", CategoryOfDataType),
                    V.GetFieldValue<int32_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);

        case DataTypeCategory::LWORD :   
        case DataTypeCategory::LINT :     
            return AllOpsOnVariables<int64_t>(
                    this->GetFieldValue<int64_t>("", CategoryOfDataType),
                    V.GetFieldValue<int64_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);

          
        case DataTypeCategory::USINT :     
            return AllOpsOnVariables<uint8_t>(
                    this->GetFieldValue<uint8_t>("", CategoryOfDataType),
                    V.GetFieldValue<uint8_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);

        case DataTypeCategory::UINT :     
            return AllOpsOnVariables<uint16_t>(
                    this->GetFieldValue<uint16_t>("", CategoryOfDataType),
                    V.GetFieldValue<uint16_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
        
        case DataTypeCategory::UDINT :     
            return AllOpsOnVariables<uint32_t>(
                    this->GetFieldValue<uint32_t>("", CategoryOfDataType),
                    V.GetFieldValue<uint32_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
        
        case DataTypeCategory::ULINT :     
            return AllOpsOnVariables<uint64_t>(
                    this->GetFieldValue<uint64_t>("", CategoryOfDataType),
                    V.GetFieldValue<uint64_t>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);

        case DataTypeCategory::CHAR :  
            if (varoptype == VarOpType::RELATIONAL) {   
                return RelationalOpOnVariables<char>(
                    this->GetFieldValue<char>("", CategoryOfDataType),
                    V.GetFieldValue<char>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else {
                __configuration->PCLogger->RaiseException("Bitwise and "
                "arithmetic ops not supported for char variables");
            }

        case DataTypeCategory::REAL : 
            if (varoptype == VarOpType::RELATIONAL) {  
                return RelationalOpOnVariables<float>(
                    this->GetFieldValue<float>("", CategoryOfDataType),
                    V.GetFieldValue<float>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else if(varoptype == VarOpType::ARITHMETIC) {
                if (VarOp == VariableOps::ADD) {
                    auto var1 = this->GetFieldValue<float>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<float>("",
                                    CategoryOfDataType);

                    auto resa = var1 + var2;
                    this->SetPCVariableField("", &resa, sizeof(resa));

                } else if (VarOp == VariableOps::SUB) {
                    auto var1 = this->GetFieldValue<float>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<float>("",
                                    CategoryOfDataType);

                    auto ress = var1 - var2;
                    this->SetPCVariableField("", &ress, sizeof(ress));

                } else if (VarOp == VariableOps::MUL) {
                    auto var1 = this->GetFieldValue<float>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<float>("",
                                    CategoryOfDataType);

                    auto resm = var1*var2;
                    this->SetPCVariableField("", &resm, sizeof(resm));

                } else if (VarOp == VariableOps::DIV) {
                    auto var1 = this->GetFieldValue<float>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<float>("",
                                    CategoryOfDataType);

                    auto resd = var1/var2;
                    this->SetPCVariableField("", &resd, sizeof(resd));

                } else {
                    __configuration->PCLogger->RaiseException("Mod operation "
                    "not supported for REAL variables!");
                }
            } else {
                __configuration->PCLogger->RaiseException("Bitwise "
                " ops not supported for real variables");
            }

        case DataTypeCategory::LREAL :     
            if (varoptype == VarOpType::RELATIONAL) {  
                return RelationalOpOnVariables<double>(
                    this->GetFieldValue<double>("", CategoryOfDataType),
                    V.GetFieldValue<double>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else if(varoptype == VarOpType::ARITHMETIC) {
                if (VarOp == VariableOps::ADD) {
                    auto var1 = this->GetFieldValue<double>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<double>("",
                                    CategoryOfDataType);

                    auto resa = var1 + var2;
                    this->SetPCVariableField("", &resa, sizeof(resa));

                } else if (VarOp == VariableOps::SUB) {
                    auto var1 = this->GetFieldValue<double>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<double>("",
                                    CategoryOfDataType);

                    auto ress = var1 - var2;
                    this->SetPCVariableField("", &ress, sizeof(ress));

                } else if (VarOp == VariableOps::MUL) {
                    auto var1 = this->GetFieldValue<double>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<double>("",
                                    CategoryOfDataType);

                    auto resm = var1*var2;
                    this->SetPCVariableField("", &resm, sizeof(resm));

                } else if (VarOp == VariableOps::DIV) {
                    auto var1 = this->GetFieldValue<double>("",
                                    CategoryOfDataType);
                    auto var2 = V.GetFieldValue<double>("",
                                    CategoryOfDataType);

                    auto resd = var1/var2;
                    this->SetPCVariableField("", &resd, sizeof(resd));

                } else {
                    __configuration->PCLogger->RaiseException("Mod operation "
                    "not supported for REAL variables!");
                }
            } else {
                __configuration->PCLogger->RaiseException("Bitwise "
                " ops not supported for LReal variables");
            }
        
        case DataTypeCategory::TIME :
            if (varoptype == VarOpType::RELATIONAL) {    
                return RelationalOpOnVariables<TimeType>(
                    this->GetFieldValue<TimeType>("", CategoryOfDataType),
                    V.GetFieldValue<TimeType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else if (varoptype == VarOpType::ARITHMETIC) { 
                return ArithmeticOpOnVariables<TimeType>(
                    this->GetFieldValue<TimeType>("", CategoryOfDataType),
                    V.GetFieldValue<TimeType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp); 
            } else {
                __configuration->PCLogger->RaiseException("Bitwise "
                " ops not supported for Time variables");
            }
        
        case DataTypeCategory::TIME_OF_DAY: 
            if (varoptype == VarOpType::RELATIONAL) {  
                return RelationalOpOnVariables<TODDataType>(
                    this->GetFieldValue<TODDataType>("", CategoryOfDataType),
                    V.GetFieldValue<TODDataType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else if (varoptype == VarOpType::ARITHMETIC) {  
                return ArithmeticOpOnVariables<TODDataType>(
                    this->GetFieldValue<TODDataType>("", CategoryOfDataType),
                    V.GetFieldValue<TODDataType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else {
                __configuration->PCLogger->RaiseException("Bitwise "
                " ops not supported for TOD variables");
            }
        
        case DataTypeCategory::DATE: 
            if (varoptype == VarOpType::RELATIONAL) {  
                return RelationalOpOnVariables<DateType>(
                    this->GetFieldValue<DateType>("", CategoryOfDataType),
                    V.GetFieldValue<DateType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else if (varoptype == VarOpType::ARITHMETIC) { 
                return ArithmeticOpOnVariables<DateType>(
                    this->GetFieldValue<DateType>("", CategoryOfDataType),
                    V.GetFieldValue<DateType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp); 
            } else {
                __configuration->PCLogger->RaiseException("Bitwise "
                " ops not supported for Date variables");
            }

        case DataTypeCategory::DATE_AND_TIME: 
            if (varoptype == VarOpType::RELATIONAL) {  
                return RelationalOpOnVariables<DateTODType>(
                    this->GetFieldValue<DateTODType>("", CategoryOfDataType),
                    V.GetFieldValue<DateTODType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp);
            } else if (varoptype == VarOpType::ARITHMETIC) { 
                return ArithmeticOpOnVariables<DateType>(
                    this->GetFieldValue<DateType>("", CategoryOfDataType),
                    V.GetFieldValue<DateType>("", CategoryOfDataType),
                    CategoryOfDataType, VarOp); 
            } else {
                __configuration->PCLogger->RaiseException("Bitwise "
                " ops not supported for DateTOD variables");
            }

        default :   __configuration->PCLogger->RaiseException(" Operation "
                        " is not supported for variables of DataType "
                        + std::to_string(CategoryOfDataType));
        
    }
    return false;
}

PCVariable& PCVariable::operator+(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::ADD);
    return *this;
}

PCVariable& PCVariable::operator-(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::SUB);
    return *this;
}

PCVariable& PCVariable::operator*(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::MUL);
    return *this;
}

PCVariable& PCVariable::operator/(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::DIV);
    return *this;
}

PCVariable& PCVariable::operator%(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::MOD);
    return *this;
}

PCVariable& PCVariable::operator&(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::AND);
    return *this;
}

PCVariable& PCVariable::operator|(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::OR);
    return *this;
}

PCVariable& PCVariable::operator^(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::XOR);
    return *this;
}

PCVariable& PCVariable::operator<<(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::LS);
    return *this;
}

PCVariable& PCVariable::operator>>(PCVariable& V ) {
    InitiateOperationOnVariables(V, VariableOps::RS);
    return *this;
}

bool operator==(PCVariable& V1, PCVariable& V2) {
    return V1.InitiateOperationOnVariables(V2, VariableOps::EQ);
}

bool operator>(PCVariable& V1, PCVariable& V2) {
    return V1.InitiateOperationOnVariables(V2, VariableOps::GT);
}

bool operator>=(PCVariable& V1, PCVariable& V2) {
    return V1.InitiateOperationOnVariables(V2, VariableOps::GE);
}

bool operator<(PCVariable& V1, PCVariable& V2) {
    return V1.InitiateOperationOnVariables(V2, VariableOps::LT);
}

bool operator<=(PCVariable& V1, PCVariable& V2) {
    return V1.InitiateOperationOnVariables(V2, VariableOps::LE);
}