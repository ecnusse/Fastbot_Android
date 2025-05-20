package com.android.commands.monkey.utils;
import com.google.gson.annotations.SerializedName;
import java.util.List;

public class StepMonkeyRequest {
    @SerializedName("block_widgets")
    private List<String> blockWidgets;

    public List<String> getBlockWidgets() {
        return blockWidgets;
    }
}
