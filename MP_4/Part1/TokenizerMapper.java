import java.io.IOException;
import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Mapper;

public class TokenizerMapper extends Mapper<LongWritable, Text, Text, LongWritable> {

    private final static LongWritable one = new LongWritable(1);

    @Override
    public void map(LongWritable key, Text value, Context context) throws IOException, InterruptedException {

        String line = value.toString();

        if (line.length() != 0) {
            if (line.charAt(0) == 'T') {
                String[] parts = line.split("\t");
                String[] s = parts[1].split(" ");
                String[] timeParts = s[1].split(":");
                String hour = timeParts[0];
                context.write(new Text(hour), one);
                // if (timeParts.length >= 2) {
                //     String hour = timeParts[0];
                //     context.write(new Text(hour), one);
                // }
            }
        }
    }
}