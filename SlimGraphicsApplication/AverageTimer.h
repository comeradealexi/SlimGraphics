#pragma once
#include <sgTypes.h>
namespace sg
{
	template <u32 GRAPH_COUNT = 150>
	class AverageTimer
	{
	public:
		AverageTimer() : average_counter(GRAPH_COUNT)
		{
		}

		inline void add_time(float time)
		{
			most_recent_time = time;
			history_buffer[history_index] = time;
			history_index = (history_index + 1) % GRAPH_COUNT;

			max_accum = std::max<float>(max_accum, time);
			min_accum = std::min<float>(min_accum, time);

			average_accum += time;
			average_counter--;
			if (average_counter == 0)
			{
				average = average_accum / static_cast<float>(GRAPH_COUNT);
				average_counter = GRAPH_COUNT;
				average_accum = 0.0f;
				min_value = min_accum;
				max_value = max_accum;

				max_accum = 0.0f;
				min_accum = 999999.0f;
			}
		}

		inline float most_recent() const { return most_recent_time; }
		inline const float* get_history_buffer() const { return history_buffer; }
		inline u32 get_history_count() const { return GRAPH_COUNT; }
		inline u32 get_history_idx() const { return history_index; }
		inline float get_average() { return average; }
		inline float min() const { return min_value; }
		inline float max() const { return max_value; }

	private:
		float most_recent_time = 0.0f;
		float max_accum = 0.0f;
		float min_accum = 999999.0f;
		float average_accum = 0.0f;
		u32 average_counter;
		u32 history_index = 0;
		float average = 0.0f;
		float min_value = 0.0f;
		float max_value = 0.0f;
		float history_buffer[GRAPH_COUNT] = {};
	};

}
