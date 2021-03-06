/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "cli/nodewizardcommand.hpp"
#include "cli/nodeutility.hpp"
#include "cli/pkiutility.hpp"
#include "cli/featureutility.hpp"
#include "base/logger.hpp"
#include "base/console.hpp"
#include "base/application.hpp"
#include "base/tlsutility.hpp"
#include "base/scriptvariable.hpp"
#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>

using namespace icinga;
namespace po = boost::program_options;

REGISTER_CLICOMMAND("node/wizard", NodeWizardCommand);

String NodeWizardCommand::GetDescription(void) const
{
	return "Wizard for Icinga 2 node setup.";
}

String NodeWizardCommand::GetShortDescription(void) const
{
	return "wizard for node setup";
}

ImpersonationLevel NodeWizardCommand::GetImpersonationLevel(void) const
{
	return ImpersonateRoot;
}

int NodeWizardCommand::GetMaxArguments(void) const
{
	return -1;
}

/**
 * The entry point for the "node wizard" CLI command.
 *
 * @returns An exit status.
 */
int NodeWizardCommand::Run(const boost::program_options::variables_map& vm, const std::vector<std::string>& ap) const
{
	/*
	 * The wizard will get all information from the user,
	 * and then call all required functions.
	 */

	std::cout << "Welcome to the Icinga 2 Setup Wizard!\n"
	    << "\n"
	    << "We'll guide you through all required configuration details.\n"
	    << "\n"
	    << "If you have questions, please consult the documentation at http://docs.icinga.org\n"
	    << "or join the community support channels at https://support.icinga.org\n"
	    << "\n\n";

	//TODO: Add sort of bash completion to path input?

	/* 0. master or node setup?
	 * 1. Ticket
	 * 2. Master information for autosigning
	 * 3. Trusted cert location
	 * 4. CN to use (defaults to FQDN)
	 * 5. Local CA
	 * 6. New self signed certificate
	 * 7. Request signed certificate from master
	 * 8. copy key information to /etc/icinga2/pki
	 * 9. enable ApiListener feature
	 * 10. generate zones.conf with endpoints and zone objects
	 * 11. set NodeName = cn in constants.conf
	 * 12. reload icinga2, or tell the user to
	 */

	std::string answer;
	bool is_node_setup = true;

	std::cout << "Please specify if this is an node setup ('no' installs a master setup) [Y/n]: ";
	std::getline (std::cin, answer);

	boost::algorithm::to_lower(answer);

	String choice = answer;

	if (choice.Contains("n"))
		is_node_setup = false;

	if (is_node_setup) {
		/* node setup part */
		std::cout << "Starting the Node setup routine...\n";

		/* CN */
		std::cout << "Please specifiy the common name (CN) [" << Utility::GetFQDN() << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			answer = Utility::GetFQDN();

		String cn = answer;
		cn.Trim();

		std::cout << "Please specifiy the local zone name [" << cn << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			answer = cn;

		String local_zone = answer;
		local_zone.Trim();

		std::vector<std::string> endpoints;

		String endpoint_buffer;

		std::cout << "Please specify the master endpoint(s) this node should connect to:\n";
		String master_endpoint_name;

wizard_endpoint_loop_start:

		std::cout << "Master CN: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if(answer.empty()) {
			Log(LogWarning, "cli", "Master CN is required! Please retry.");
			goto wizard_endpoint_loop_start;
		}

		endpoint_buffer = answer;
		endpoint_buffer.Trim();

		std::cout << "Master endpoint host: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (!answer.empty()) {
			String tmp = answer;
			tmp.Trim();
			endpoint_buffer += "," + tmp;
			master_endpoint_name = tmp; //store the endpoint name for later
		}

		std::cout << "Master endpoint port (optional) []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (!answer.empty()) {
			String tmp = answer;
			tmp.Trim();
			endpoint_buffer += "," + answer;
		}

		endpoints.push_back(endpoint_buffer);

		std::cout << "Add more master endpoints? [y/N]";
		std::getline (std::cin, answer);

		boost::algorithm::to_lower(answer);

		String choice = answer;

		if (choice.Contains("y") || choice.Contains("j"))
			goto wizard_endpoint_loop_start;

		std::cout << "Please specify the master connection for auto-signing:\n";

wizard_master_host:
		std::cout << "Host [" << master_endpoint_name << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty() && !master_endpoint_name.IsEmpty())
			answer = master_endpoint_name;

		if (answer.empty() && master_endpoint_name.IsEmpty())
			goto wizard_master_host;

		String master_host = answer;
		master_host.Trim();

		std::cout << "Port [5665]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			answer = "5665";

		String master_port = answer;
		master_port.Trim();

		/* workaround for fetching the master cert */
		String pki_path = PkiUtility::GetPkiPath();
		String node_cert = pki_path + "/" + cn + ".crt";
		String node_key = pki_path + "/" + cn + ".key";

		//new-ca, new-cert
		PkiUtility::NewCa();

		if (!Utility::MkDirP(pki_path, 0700)) {
			Log(LogCritical, "cli")
			    << "Could not create local pki directory '" << pki_path << "'.";
			return 1;
		}

		String user = ScriptVariable::Get("RunAsUser");
		String group = ScriptVariable::Get("RunAsGroup");

		if (!Utility::SetFileOwnership(pki_path, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << pki_path << "'. Verify it yourself!";
		}

		if (PkiUtility::NewCert(cn, node_key, Empty, node_cert) > 0) {
			Log(LogCritical, "cli")
			    << "Failed to create new self-signed certificate for CN '" << cn << "'. Please try again.";
			return 1;
		}

		/* store ca in /etc/icinga2/pki */
		String ca_path = PkiUtility::GetLocalCaPath();
		String ca_key = ca_path + "/ca.key";
		String ca = ca_path + "/ca.crt";

		/* fix permissions: root -> icinga daemon user */
		if (!Utility::SetFileOwnership(ca_path, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << ca_path << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(ca, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << ca << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(ca_key, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << ca_key << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(node_cert, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << node_cert << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(node_key, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << node_key << "'. Verify it yourself!";
		}

		String target_ca = pki_path + "/ca.crt";

		Utility::CopyFile(ca, target_ca);

		/* fix permissions: root -> icinga daemon user */
		if (!Utility::SetFileOwnership(target_ca, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << target_ca << "'. Verify it yourself!";
		}

		//save-cert and store the master certificate somewhere

		std::cout << "Generating self-signed certifiate:\n";


		std::cout << "Fetching public certificate from master ("
		    << master_host << ", " << master_port << "):\n";

		String trusted_cert = PkiUtility::GetPkiPath() + "/trusted-master.crt";

		if (PkiUtility::SaveCert(master_host, master_port, node_key, node_cert, trusted_cert) > 0) {
			Log(LogCritical, "cli")
			    << "Failed to fetch trusted master certificate. Please try again.";
			return 1;
		}

		std::cout << "Stored trusted master certificate in '" << trusted_cert << "'.\n";

wizard_ticket:
		std::cout << "Please specify the request ticket generated on your Icinga 2 master."
		    << " (Hint: '# icinga2 pki ticket --cn " << cn << "'):\n";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			goto wizard_ticket;

		String ticket = answer;
		ticket.Trim();

		std::cout << "Processing self-signed certificate request. Ticket '" << ticket << "'.\n";

		if (PkiUtility::RequestCertificate(master_host, master_port, node_key, node_cert, ca, trusted_cert, ticket) > 0) {
			Log(LogCritical, "cli")
			    << "Failed to fetch signed certificate from master '" << master_host << ", "
			    << master_port <<"'. Please try again.";
			return 1;
		}

		/* fix permissions (again) when updating the signed certificate */
		if (!Utility::SetFileOwnership(node_cert, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << node_cert << "'. Verify it yourself!";
		}

		/* apilistener config */
		std::cout << "Please specify the API bind host/port (optional):\n";
		std::cout << "Bind Host []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_host = answer;
		bind_host.Trim();

		std::cout << "Bind Port []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_port = answer;
		bind_port.Trim();

		std::cout << "Enabling the APIlistener feature.\n";

		std::vector<std::string> enable;
		enable.push_back("api");
		FeatureUtility::EnableFeatures(enable);

		String apipath = FeatureUtility::GetFeaturesAvailablePath() + "/api.conf";
		NodeUtility::CreateBackupFile(apipath);

		String apipathtmp = apipath + ".tmp";

		std::ofstream fp;
		fp.open(apipathtmp.CStr(), std::ofstream::out | std::ofstream::trunc);

		fp << "/**\n"
		    << " * The API listener is used for distributed monitoring setups.\n"
		    << " */\n"
		    << "object ApiListener \"api\" {\n"
		    << "  cert_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".crt\"\n"
		    << "  key_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".key\"\n"
		    << "  ca_path = SysconfDir + \"/icinga2/pki/ca.crt\"\n";

		if (!bind_host.IsEmpty())
			fp << "  bind_host = \"" << bind_host << "\"\n";
		if (!bind_port.IsEmpty())
			fp << "  bind_port = " << bind_port << "\n";

		fp << "\n"
		    << "  ticket_salt = TicketSalt\n"
		    << "}\n";

		fp.close();

#ifdef _WIN32
		_unlink(apipath.CStr());
#endif /* _WIN32 */

		if (rename(apipathtmp.CStr(), apipath.CStr()) < 0) {
			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("rename")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(apipathtmp));
		}

		/* apilistener config */
		std::cout << "Generating local zones.conf.\n";

		NodeUtility::GenerateNodeIcingaConfig(endpoints, cn, local_zone);

		if (cn != Utility::GetFQDN()) {
			Log(LogWarning, "cli")
			    << "CN '" << cn << "' does not match the default FQDN '" << Utility::GetFQDN() << "'. Requires update for NodeName constant in constants.conf!";
		}

		std::cout << "Updating constants.conf\n";

		NodeUtility::CreateBackupFile(Application::GetSysconfDir() + "/icinga2/constants.conf");

		NodeUtility::UpdateConstant("NodeName", cn);

	} else {
		/* master setup */
		std::cout << "Starting the Master setup routine...\n";

		/* CN */
		std::cout << "Please specifiy the common name (CN) [" << Utility::GetFQDN() << "]: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		if (answer.empty())
			answer = Utility::GetFQDN();

		String cn = answer;
		cn.Trim();

		if (PkiUtility::NewCa() > 0) {
			Log(LogWarning, "cli", "Found CA, skipping and using the existing one.");
		}

		String pki_path = PkiUtility::GetPkiPath();

		if (!Utility::MkDirP(pki_path, 0700)) {
			Log(LogCritical, "cli")
			    << "Could not create local pki directory '" << pki_path << "'.";
			return 1;
		}

		String user = ScriptVariable::Get("RunAsUser");
		String group = ScriptVariable::Get("RunAsGroup");

		if (!Utility::SetFileOwnership(pki_path, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << pki_path << "'. Verify it yourself!";
		}

		String key = pki_path + "/" + cn + ".key";
		String csr = pki_path + "/" + cn + ".csr";

		if (PkiUtility::NewCert(cn, key, csr, "") > 0) {
			Log(LogCritical, "cli", "Failed to create self-signed certificate");
			return 1;
		}

		/* Sign the CSR with the CA key */

		String cert = pki_path + "/" + cn + ".crt";

		if (PkiUtility::SignCsr(csr, cert) != 0) {
			Log(LogCritical, "cli", "Could not sign CSR.");
			return 1;
		}

		/* Copy CA certificate to /etc/icinga2/pki */

		String ca_path = PkiUtility::GetLocalCaPath();
		String ca = ca_path + "/ca.crt";
		String ca_key = ca_path + "/ca.key";
		String target_ca = pki_path + "/ca.crt";

		Log(LogInformation, "cli")
		    << "Copying CA certificate to '" << target_ca << "'.";

		/* does not overwrite existing files! */
		Utility::CopyFile(ca, target_ca);

		/* fix permissions: root -> icinga daemon user */
		if (!Utility::SetFileOwnership(ca_path, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << ca_path << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(ca, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << ca << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(ca_key, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << ca_key << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(target_ca, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << target_ca << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(key, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << key << "'. Verify it yourself!";
		}
		if (!Utility::SetFileOwnership(csr, user, group)) {
			Log(LogWarning, "cli")
			    << "Cannot set ownership for user '" << user << "' group '" << group << "' on file '" << csr << "'. Verify it yourself!";
		}

		NodeUtility::GenerateNodeMasterIcingaConfig(cn);

		/* apilistener config */
		std::cout << "Please specify the API bind host/port (optional):\n";
		std::cout << "Bind Host []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_host = answer;
		bind_host.Trim();

		std::cout << "Bind Port []: ";

		std::getline(std::cin, answer);
		boost::algorithm::to_lower(answer);

		String bind_port = answer;
		bind_port.Trim();

		std::cout << "Enabling the APIlistener feature.\n";

		std::vector<std::string> enable;
		enable.push_back("api");
		FeatureUtility::EnableFeatures(enable);

		String apipath = FeatureUtility::GetFeaturesAvailablePath() + "/api.conf";
		NodeUtility::CreateBackupFile(apipath);

		String apipathtmp = apipath + ".tmp";

		std::ofstream fp;
		fp.open(apipathtmp.CStr(), std::ofstream::out | std::ofstream::trunc);

		fp << "/**\n"
		    << " * The API listener is used for distributed monitoring setups.\n"
		    << " */\n"
		    << "object ApiListener \"api\" {\n"
		    << "  cert_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".crt\"\n"
		    << "  key_path = SysconfDir + \"/icinga2/pki/\" + NodeName + \".key\"\n"
		    << "  ca_path = SysconfDir + \"/icinga2/pki/ca.crt\"\n";

		if (!bind_host.IsEmpty())
			fp << "  bind_host = \"" << bind_host << "\"\n";
		if (!bind_port.IsEmpty())
			fp << "  bind_port = " << bind_port << "\n";

		fp << "\n"
		    << "  ticket_salt = TicketSalt\n"
		    << "}\n";

		fp.close();

#ifdef _WIN32
		_unlink(apipath.CStr());
#endif /* _WIN32 */

		if (rename(apipathtmp.CStr(), apipath.CStr()) < 0) {
			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("rename")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(apipathtmp));
		}

		/* update constants.conf with NodeName = CN + TicketSalt = random value */
		if (cn != Utility::GetFQDN()) {
			Log(LogWarning, "cli")
				<< "CN '" << cn << "' does not match the default FQDN '" << Utility::GetFQDN() << "'. Requires update for NodeName constant in constants.conf!";
		}

		Log(LogInformation, "cli", "Updating constants.conf.");

		NodeUtility::CreateBackupFile(Application::GetSysconfDir() + "/icinga2/constants.conf");

		NodeUtility::UpdateConstant("NodeName", cn);

		String salt = RandomString(16);

		NodeUtility::UpdateConstant("TicketSalt", salt);

		Log(LogInformation, "cli")
		    << "Edit the api feature config file '" << apipath << "' and set a secure 'ticket_salt' attribute.";
	}

	std::cout << "Now restart your Icinga 2 to finish the installation!\n";

	std::cout << "If you encounter problems or bugs, please do not hesitate to\n"
	    << "get in touch with the community at https://support.icinga.org" << std::endl;

	return 0;
}
