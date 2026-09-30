// crates/component-shell/src/shell/input_group/content_type.rs

#include "component_shell/input_group/mod.h"

namespace gpui::component_shell::input_group::content_type {

// In InputContentType's order, which is what the index maps back to.
const char* const kLiterals[45] = {
    "name",
    "name_prefix",
    "given_name",
    "middle_name",
    "family_name",
    "name_suffix",
    "nickname",
    "job_title",
    "organization_name",
    "location",
    "full_street_address",
    "street_address_line1",
    "street_address_line2",
    "address_city",
    "address_state",
    "address_city_and_state",
    "sublocality",
    "country_name",
    "postal_code",
    "telephone_number",
    "email_address",
    "url",
    "credit_card_number",
    "credit_card_name",
    "credit_card_given_name",
    "credit_card_middle_name",
    "credit_card_family_name",
    "credit_card_security_code",
    "credit_card_expiration",
    "credit_card_expiration_month",
    "credit_card_expiration_year",
    "credit_card_type",
    "username",
    "password",
    "new_password",
    "one_time_code",
    "shipment_tracking_number",
    "flight_number",
    "date_time",
    "birthdate",
    "birthdate_day",
    "birthdate_month",
    "birthdate_year",
    "cellular_eid",
    "cellular_imei",
};

static_assert((int)component::InputContentType::CellularImei == 44,
              "kLiterals follows InputContentType");

bool Record(PayloadBuild* build, const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum) {
        StrBuilder expected;
        for (int i = 0; i < 45; i++) {
            if (i) expected.Append(StrL(", "));
            expected.Append(Str(kLiterals[i]));
        }
        Str list = expected.TakeStr();
        bool failed = build->Fail(fmt("content_type expects one of %s", list));
        StrFree(list);
        return failed;
    }
    for (int i = 0; i < 45; i++) {
        if (!StrEq(args[0].string, kLiterals[i])) continue;
        Op* op = build->New<Op>();
        op->kind = Op::ContentType;
        op->contentType = (component::InputContentType)i;
        return true;
    }
    return build->Fail(fmt("unsupported content_type `%s`", args[0].string));
}

} // namespace gpui::component_shell::input_group::content_type
