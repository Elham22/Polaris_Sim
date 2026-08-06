file = "./15_geo_rel.xml"
out_file = "./15_geo_rel_annotated.xml"

node_type_line = '<property name="type" type="string">core</property>\n'
link_type_line = '<property name="rel" type="string">core</property>\n'

with open(file, "r") as f:
    lines = f.readlines();
    with open(out_file, "w+") as f_out:
        for line in lines:
            if not ("customer" in line or "peer" in line):
                f_out.write(line)
                if "node" in line and "id" in line:
                    f_out.write(node_type_line)
                if '<to type="int"' in line:
                    f_out.write(link_type_line)
