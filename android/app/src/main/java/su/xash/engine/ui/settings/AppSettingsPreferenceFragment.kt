package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.navigation.fragment.findNavController
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import su.xash.engine.R

class AppSettingsPreferenceFragment() : PreferenceFragmentCompat() {
	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceManager.sharedPreferencesName = "app_preferences";
		setPreferencesFromResource(R.xml.app_preferences, rootKey);

		val defaultPath = requireContext().getExternalFilesDir(null)?.absolutePath
			?: requireContext().filesDir.absolutePath
		val gamePath = preferenceManager.sharedPreferences
			?.getString("game_path", null) ?: defaultPath
		findPreference<Preference>("game_path")?.summary = gamePath

		findPreference<Preference>("crash_logs")?.setOnPreferenceClickListener {
			findNavController().navigate(R.id.action_appSettingsFragment_to_crashLogsFragment)
			true
		}
	}
}
