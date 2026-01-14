#!/bin/bash

# Create docs directory if it doesn't exist
mkdir -p docs

# Array of topic names
topics=(
    "Modbus_Protocol_Overview"
    "Modbus_RTU_Frame_Structure"
    "Modbus_ASCII_Frame_Structure"
    "Modbus_TCP_IP_Frame_Structure"
    "CRC_16_Calculation_Algorithm"
    "LRC_Calculation_Algorithm"
    "Function_Code_0x01_Read_Coils"
    "Function_Code_0x02_Read_Discrete_Inputs"
    "Function_Code_0x03_Read_Holding_Registers"
    "Function_Code_0x04_Read_Input_Registers"
    "Function_Code_0x05_Write_Single_Coil"
    "Function_Code_0x06_Write_Single_Register"
    "Function_Code_0x0F_Write_Multiple_Coils"
    "Function_Code_0x10_Write_Multiple_Registers"
    "Function_Code_0x17_Read_Write_Multiple_Registers"
    "Exception_Response_Handling"
    "RS_485_Hardware_Configuration"
    "Serial_Port_Configuration"
    "Serial_Timeout_Management"
    "Serial_Port_Libraries"
    "RTU_3_5_Character_Gap_Detection"
    "Multi_Drop_Network_Configuration"
    "TCP_Socket_Programming"
    "TCP_Connection_Management"
    "Modbus_TCP_Port_502"
    "Transaction_Identifier_Management"
    "Multiple_Client_Handling"
    "TCP_Keep_Alive_Mechanism"
    "Big_Endian_vs_Little_Endian"
    "32_bit_Integer_Handling"
    "IEEE_754_Floating_Point"
    "Bit_Manipulation"
    "Register_Scaling_and_Conversion"
    "String_Data_Handling"
    "Slave_Server_Implementation"
    "Master_Client_Implementation"
    "Data_Model_and_Address_Mapping"
    "Register_Database_Design"
    "Polling_Strategies"
    "Request_Queuing"
    "Retry_Logic_and_Timeout_Handling"
    "Error_Detection_and_Recovery"
    "Thread_Safety_and_Concurrency"
    "Asynchronous_IO"
    "Performance_Optimization"
    "Circular_Buffer_Implementation"
    "Modbus_Security_Considerations"
    "Protocol_Analyzers_and_Debugging"
    "Diagnostic_Counters"
    "Unit_Testing_Strategies"
)

# Generate files with zero-padded numbering
for i in "${!topics[@]}"; do
    # Calculate file number (i+1) with zero padding
    file_num=$(printf "%02d" $((i + 1)))
    filename="docs/${file_num}_${topics[$i]}.md"
    
    # Create empty file
    touch "$filename"
    echo "Created: $filename"
done

echo ""
echo "Successfully created ${#topics[@]} documentation files in the docs/ directory"