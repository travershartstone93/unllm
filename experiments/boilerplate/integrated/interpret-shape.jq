def valid_cli_roles:
  type=="object" and (.input_type|type=="string") and (.roles|type=="array" and length>0) and
  (.roles|map(.id)|unique|length)==(.roles|length) and all(.roles[];
  (.id|type=="string" and test("^[a-zA-Z0-9_-]+$")) and (.text|type=="string" and length>0) and
  (.output_type|type=="string") and (.multiple|type=="boolean") and
  (.content|IN("whole","text","embedded_images","pages","metadata","audio","video","other")));
