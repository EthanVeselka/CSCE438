import java.io.IOException;

import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Mapper;

public class TokenizerMapper extends Mapper<LongWritable, Text, Text, LongWritable> {

    private final static LongWritable one = new LongWritable(1);
    private Text hour = new Text();

    @Override
    public void map(LongWritable key, Text value, Context context) throws IOException, InterruptedException {
        String[] fields = value.toString().split("\n");
        String[] timestamp;
        String content = "";
        for (String line : fields) {
            if (line.startsWith("T")) {
                timestamp = line.split("\t");
                String[] s = timestamp[1].split(" ");
                String[] parts = s[1].split(":");
                hour.set(parts[0]);
            }
            else if (line.startsWith("W")) {
                content = line.split("\t")[1];
            }
        }
        if (content.toLowerCase().contains("sleep")) {
            context.write(hour, one);
        }
    }
}

