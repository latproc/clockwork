# Regression fixture for private CONSTANT command and Modbus visibility.
PUBLIC_VALUE CONSTANT "visible-value";
PRIVATE_VALUE CONSTANT(private:true) "do-not-display-this-value";
PRIVATE_EXPORTED_VALUE CONSTANT(private:true, export:str, strlen:32) "do-not-export-this-value";
